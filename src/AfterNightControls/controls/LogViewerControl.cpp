// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LogViewerControl.h"
#include "LogViewerSettingsControl.h"
#include "LogViewerFindTextControl.h"
#include "app/ApplicationStorage.h"
#include "app/FileDialogUtils.h"
#include "app/SessionConsoleBridge.h"
#include "app/UiStyleTokens.h"
#include "controls/IAppContext.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QPalette>
#include <QScrollBar>

#include <optional>

namespace
{

constexpr QChar kAnsiEscapeCharacter(0x1b);
constexpr auto kAnsiColorFormattingSettingKey = "log/render_ansi_colors";

struct AnsiTextStyle
{
    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;
    bool inverse = false;
    std::optional<QColor> foreground;
    std::optional<QColor> background;
};

[[nodiscard]] an::ui::IIconProviderPtr currentIconProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getIconProvider();
    }

    return {};
}

[[nodiscard]] QIcon iconForName(const QString& iconName)
{
    if (auto iconProvider = currentIconProvider())
    {
        return iconProvider->getIcon(iconName);
    }

    return {};
}

[[nodiscard]] an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

[[nodiscard]] an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

[[nodiscard]] an::ui::IPathMemoryService* currentPathMemoryService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getPathMemoryService();
    }

    return nullptr;
}

QColor blend(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  lerp(base.alpha(), tint.alpha()));
}

QColor normalizeAnsiBackgroundColor(QColor color, const an::ui::styling::ThemeVariant themeVariant)
{
    color.setAlpha(themeVariant == an::ui::styling::ThemeVariant::Dark ? 112 : 72);
    return color;
}

QColor ansiBaseColor(const an::ui::styling::ThemeVariant themeVariant, const int index, const bool bright)
{
    using an::ui::styling::SemanticColorRole;
    const auto semantic = [themeVariant](const SemanticColorRole role) {
        return an::ui::styling::semanticColor(themeVariant, role);
    };

    switch (index)
    {
    case 0:
        return bright ? blend(semantic(SemanticColorRole::TextPrimary), semantic(SemanticColorRole::CodeBackground),
                              themeVariant == an::ui::styling::ThemeVariant::Dark ? 0.22 : 0.08)
                      : blend(semantic(SemanticColorRole::TextPrimary), semantic(SemanticColorRole::CodeBackground),
                              themeVariant == an::ui::styling::ThemeVariant::Dark ? 0.55 : 0.32);
    case 1:
        return bright ? semantic(SemanticColorRole::StatusError) : semantic(SemanticColorRole::DangerText);
    case 2:
        return bright ? semantic(SemanticColorRole::StatusSuccess) : semantic(SemanticColorRole::SuccessText);
    case 3:
        return bright ? semantic(SemanticColorRole::StatusWarning) : semantic(SemanticColorRole::WarningText);
    case 4:
        return bright ? blend(semantic(SemanticColorRole::AccentPrimary), semantic(SemanticColorRole::InfoText), 0.45)
                      : semantic(SemanticColorRole::InfoText);
    case 5:
        return blend(semantic(SemanticColorRole::AccentPrimary), semantic(SemanticColorRole::DangerText),
                     bright ? 0.30 : 0.48);
    case 6:
        return blend(semantic(SemanticColorRole::InfoText), semantic(SemanticColorRole::SuccessText),
                     bright ? 0.45 : 0.28);
    case 7:
        return bright ? semantic(SemanticColorRole::TextPrimary)
                      : blend(semantic(SemanticColorRole::TextPrimary), semantic(SemanticColorRole::CodeBackground),
                              themeVariant == an::ui::styling::ThemeVariant::Dark ? 0.12 : 0.18);
    default:
        return semantic(SemanticColorRole::TextPrimary);
    }
}

QColor ansiStandardColor(const an::ui::styling::ThemeVariant themeVariant, const int index, const bool bright,
                         const bool background)
{
    QColor color = ansiBaseColor(themeVariant, index, bright);
    if (background)
    {
        color = normalizeAnsiBackgroundColor(color, themeVariant);
    }
    return color;
}

QColor ansi256Color(const int index)
{
    if (index < 0)
    {
        return QColor();
    }

    if (index < 16)
    {
        static const std::array<QColor, 16> palette = {
            QColor(0, 0, 0),       QColor(205, 49, 49),   QColor(13, 188, 121), QColor(229, 229, 16),
            QColor(36, 114, 200),  QColor(188, 63, 188),  QColor(17, 168, 205), QColor(229, 229, 229),
            QColor(102, 102, 102), QColor(241, 76, 76),   QColor(35, 209, 139), QColor(245, 245, 67),
            QColor(59, 142, 234),  QColor(214, 112, 214), QColor(41, 184, 219), QColor(255, 255, 255)};

        return palette[static_cast<std::size_t>(index)];
    }

    if (index >= 16 && index <= 231)
    {
        const int cubeIndex = index - 16;
        const int red = cubeIndex / 36;
        const int green = (cubeIndex / 6) % 6;
        const int blue = cubeIndex % 6;
        const auto component = [](const int value) { return value == 0 ? 0 : 55 + value * 40; };
        return QColor(component(red), component(green), component(blue));
    }

    if (index >= 232 && index <= 255)
    {
        const int gray = 8 + (index - 232) * 10;
        return QColor(gray, gray, gray);
    }

    return QColor();
}

QVector<int> parseAnsiCodes(const QString& parameterText)
{
    if (parameterText.isEmpty())
    {
        return {0};
    }

    QVector<int> codes;
    const QStringList parts = parameterText.split(';', Qt::KeepEmptyParts);
    codes.reserve(parts.size());
    for (const QString& part : parts)
    {
        if (part.isEmpty())
        {
            codes.push_back(0);
            continue;
        }

        bool ok = false;
        const int value = part.toInt(&ok);
        if (ok)
        {
            codes.push_back(value);
        }
    }

    if (codes.isEmpty())
    {
        codes.push_back(0);
    }

    return codes;
}

void applyAnsiCodes(AnsiTextStyle& style, const QVector<int>& codes, const an::ui::styling::ThemeVariant themeVariant)
{
    for (int index = 0; index < codes.size(); ++index)
    {
        const int code = codes[index];
        switch (code)
        {
        case 0:
            style = {};
            break;
        case 1:
            style.bold = true;
            break;
        case 2:
            style.faint = true;
            break;
        case 3:
            style.italic = true;
            break;
        case 4:
            style.underline = true;
            break;
        case 7:
            style.inverse = true;
            break;
        case 22:
            style.bold = false;
            style.faint = false;
            break;
        case 23:
            style.italic = false;
            break;
        case 24:
            style.underline = false;
            break;
        case 27:
            style.inverse = false;
            break;
        case 39:
            style.foreground.reset();
            break;
        case 49:
            style.background.reset();
            break;
        default:
            if (code >= 30 && code <= 37)
            {
                style.foreground = ansiStandardColor(themeVariant, code - 30, false, false);
                break;
            }

            if (code >= 40 && code <= 47)
            {
                style.background = ansiStandardColor(themeVariant, code - 40, false, true);
                break;
            }

            if (code >= 90 && code <= 97)
            {
                style.foreground = ansiStandardColor(themeVariant, code - 90, true, false);
                break;
            }

            if (code >= 100 && code <= 107)
            {
                style.background = ansiStandardColor(themeVariant, code - 100, true, true);
                break;
            }

            if ((code == 38 || code == 48) && index + 1 < codes.size())
            {
                const bool foreground = code == 38;
                const int mode = codes[++index];
                if (mode == 5 && index + 1 < codes.size())
                {
                    QColor color = ansi256Color(codes[++index]);
                    if (color.isValid())
                    {
                        if (foreground)
                        {
                            style.foreground = color;
                        }
                        else
                        {
                            style.background = normalizeAnsiBackgroundColor(color, themeVariant);
                        }
                    }
                }
                else if (mode == 2 && index + 3 < codes.size())
                {
                    QColor color(codes[index + 1], codes[index + 2], codes[index + 3]);
                    index += 3;
                    if (foreground)
                    {
                        style.foreground = color;
                    }
                    else
                    {
                        style.background = normalizeAnsiBackgroundColor(color, themeVariant);
                    }
                }
            }
            break;
        }
    }
}

QTextCharFormat ansiTextFormat(const QTextCharFormat& baseFormat, const AnsiTextStyle& style,
                               const QColor& baseForeground, const QColor& baseBackground,
                               const an::ui::styling::ThemeVariant themeVariant)
{
    QTextCharFormat format(baseFormat);

    QColor foreground = style.foreground.value_or(baseForeground);
    std::optional<QColor> background = style.background;
    if (style.inverse)
    {
        const QColor swappedBackground = normalizeAnsiBackgroundColor(foreground, themeVariant);
        foreground = background.value_or(baseBackground);
        background = swappedBackground;
    }

    if (style.faint)
    {
        foreground =
            blend(foreground, baseBackground, themeVariant == an::ui::styling::ThemeVariant::Dark ? 0.42 : 0.56);
    }

    format.setForeground(foreground);
    format.setBackground(background.has_value() ? QBrush(*background) : QBrush(Qt::NoBrush));
    format.setFontWeight(style.bold ? QFont::Bold : baseFormat.fontWeight());
    format.setFontItalic(style.italic || baseFormat.fontItalic());
    format.setFontUnderline(style.underline);
    return format;
}

QString insertAnsiAwareText(QTextCursor& cursor, const QString& text, const QTextCharFormat& baseFormat,
                            const QColor& baseForeground, const QColor& baseBackground,
                            const an::ui::styling::ThemeVariant themeVariant, const bool renderAnsiFormatting)
{
    QString plainText;
    plainText.reserve(text.size());

    QString pendingText;
    pendingText.reserve(text.size());
    AnsiTextStyle currentStyle;

    const auto flushPendingText = [&]() {
        if (pendingText.isEmpty())
        {
            return;
        }

        plainText += pendingText;
        const QTextCharFormat format = renderAnsiFormatting ? ansiTextFormat(baseFormat, currentStyle, baseForeground,
                                                                             baseBackground, themeVariant)
                                                            : baseFormat;
        cursor.insertText(pendingText, format);
        pendingText.clear();
    };

    for (int index = 0; index < text.size();)
    {
        if (text[index] == kAnsiEscapeCharacter && index + 1 < text.size() && text[index + 1] == '[')
        {
            int finalIndex = index + 2;
            while (finalIndex < text.size())
            {
                const QChar ch = text[finalIndex];
                if ((ch.unicode() >= 0x40) && (ch.unicode() <= 0x7e))
                {
                    break;
                }
                ++finalIndex;
            }

            if (finalIndex < text.size())
            {
                const QChar command = text[finalIndex];
                flushPendingText();
                if (command == 'm')
                {
                    applyAnsiCodes(currentStyle, parseAnsiCodes(text.mid(index + 2, finalIndex - index - 2)),
                                   themeVariant);
                }

                index = finalIndex + 1;
                continue;
            }
        }

        if (text[index] != kAnsiEscapeCharacter)
        {
            pendingText += text[index];
        }
        ++index;
    }

    flushPendingText();
    return plainText;
}

an::ui::styling::ThemeVariant resolveThemeVariant()
{
    if (QGuiApplication::instance())
    {
        const bool darkPalette = QGuiApplication::palette().color(QPalette::Window).value() < 128;
        return darkPalette ? an::ui::styling::ThemeVariant::Dark : an::ui::styling::ThemeVariant::Light;
    }

    return an::ui::styling::ThemeVariant::Dark;
}

QColor semanticLogLevelColor(const an::ui::styling::ThemeVariant themeVariant, const spdlog::level::level_enum level)
{
    using an::ui::styling::SemanticColorRole;
    const auto semantic = [themeVariant](const SemanticColorRole role) {
        return an::ui::styling::semanticColor(themeVariant, role);
    };

    switch (level)
    {
    case spdlog::level::trace:
        return semantic(SemanticColorRole::TextMuted);
    case spdlog::level::debug:
        return semantic(SemanticColorRole::TextSecondary);
    case spdlog::level::info:
        return semantic(SemanticColorRole::StatusActive);
    case spdlog::level::warn:
        return semantic(SemanticColorRole::StatusWarning);
    case spdlog::level::err:
        return semantic(SemanticColorRole::StatusError);
    case spdlog::level::critical:
        return blend(semantic(SemanticColorRole::StatusError), semantic(SemanticColorRole::AccentPrimary), 0.32);
    default:
        return semantic(SemanticColorRole::TextPrimary);
    }
}

LogViewerControl::ThemeColorDefaults resolveThemeDefaults()
{
    const auto themeVariant = resolveThemeVariant();
    using an::ui::styling::SemanticColorRole;
    const auto semantic = [themeVariant](const SemanticColorRole role) {
        return an::ui::styling::semanticColor(themeVariant, role);
    };

    LogViewerControl::ThemeColorDefaults defaults;
    defaults.fontColor = semantic(SemanticColorRole::TextPrimary);
    defaults.backgroundColor = semantic(SemanticColorRole::CodeBackground);
    defaults.fontHighlightColor = semantic(SemanticColorRole::TextOnAccent);
    defaults.backgroundHighlightColor = semantic(SemanticColorRole::HighlightBackground);
    return defaults;
}

QColor parseColorOrFallback(const QString& value, const QColor& fallback)
{
    const QColor parsed(value);
    return parsed.isValid() ? parsed : fallback;
}

} // anonymous namespace

LogViewerControl::LogViewerControl(QWidget* parent, Qt::WindowFlags f)
    : DockableControl(DockableControlType::CONSOLE_LOG, parent, f)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
}

KDDockWidgets::QtWidgets::DockWidget* LogViewerControl::getDockWidget()
{
    if (!m_DockWidget)
    {
        m_DockWidget = new CustomDockWidget("Session Console");
        m_DockWidget->setWidget(this);

        connect(reinterpret_cast<CustomDockWidget*>(m_DockWidget), &CustomDockWidget::closeRequested, this,
                &LogViewerControl::closeRequested);

        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isCurrentTabChanged, this,
                &LogViewerControl::isCurrentTabChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isFocusedChanged, this,
                &LogViewerControl::isFocusedChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged, this,
                &LogViewerControl::isFloatingChanged);
        connect(m_DockWidget, &KDDockWidgets::QtWidgets::DockWidget::guestViewChanged, this,
                &LogViewerControl::guestViewChanged);
    }

    return m_DockWidget;
}

void LogViewerControl::initialize()
{
    QSettings settings = an::ui::applicationSettings();

    if (!settings.contains(kAnsiColorFormattingSettingKey))
    {
        settings.setValue(kAnsiColorFormattingSettingKey, true);
    }

    m_IsShutdown = false;
    m_AnsiColorFormattingEnabled = settings.value(kAnsiColorFormattingSettingKey).toBool();

    this->setAttribute(Qt::WA_DeleteOnClose);

    m_CanvasLayout = new QVBoxLayout(this);
    setLayout(m_CanvasLayout);

    // Create the toolbar
    auto tb = new QToolBar();

    tb->setAutoFillBackground(true);

    const auto iconFor = [](const QString& iconName) { return iconForName(iconName); };

    /*auto actionZoomIn = */ tb->addAction(iconFor("zoom_in"), "Zoom In", this, &LogViewerControl::zoomIn);
    // actionZoomIn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    /*auto actionZoomOut = */ tb->addAction(iconFor("zoom_out"), "Zoom Out", this, &LogViewerControl::zoomOut);
    // actionZoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    /*auto actionZoom100 =*/tb->addAction(iconFor("zoom_100"), "Zoom 100%", this, &LogViewerControl::zoom100);
    // actionZoom100->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    tb->addSeparator();
    m_WordWrapAction =
        tb->addAction(iconFor("format_align_left"), "Enable word wrap", this, &LogViewerControl::wordWrap);
    tb->addAction(iconFor("delete"), "Clear Session", this, &LogViewerControl::clearContent);
    tb->addAction(iconFor("refresh2"), "Reload session lines", this, &LogViewerControl::reloadContent);
    m_AnsiColorsAction = tb->addAction(iconFor("terminal"), "ANSI Colors");
    m_AnsiColorsAction->setCheckable(true);
    m_AnsiColorsAction->setChecked(m_AnsiColorFormattingEnabled);
    m_AnsiColorsAction->setToolTip(tr("Render ANSI terminal color sequences in session output when present."));

    tb->addSeparator();
    m_SettingsAction = tb->addAction(iconFor("settings"), "Settings", this, &LogViewerControl::settings);

    m_FilterLabel = new QLabel(tr("Show >="), tb);
    m_FilterLabel->setToolTip(tr("Filters visible session entries only."));
    tb->addWidget(m_FilterLabel);

    m_LevelComboBox = std::make_unique<QComboBox>();
    m_LevelComboBox->setMinimumWidth(85);
    m_LevelComboBox->addItem("error");
    m_LevelComboBox->addItem("warn");
    m_LevelComboBox->addItem("info");
    m_LevelComboBox->addItem("debug");
    m_LevelComboBox->addItem("trace");

    m_LevelComboBox->setCurrentText("info");

    QObject::connect(m_LevelComboBox.get(), &QComboBox::currentTextChanged, this,
                     &LogViewerControl::on_level_currentTextChanged);

    m_LevelComboBox->setToolTip(tr("Filters visible session entries only. Does not change file logging."));

    tb->addWidget(m_LevelComboBox.get());

    tb->addSeparator();
    m_FileLevelIndicator = new QLabel(tb);
    m_FileLevelIndicator->setToolTip(tr("Current file logging level from Settings > Logging."));
    tb->addWidget(m_FileLevelIndicator);

    tb->addSeparator();

    m_SearchAction = tb->addAction(iconFor("search"), "Search...", this, &LogViewerControl::search);
    m_SearchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));

    auto actionSave = tb->addAction(iconFor("file_save"), "Export visible session text", this, &LogViewerControl::save);
    actionSave->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));

    m_TextEdit = new QPlainTextEdit(this);

    m_TextEdit->setReadOnly(true);
    m_TextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_TextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_TextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_TextEdit->setUndoRedoEnabled(false);

    auto lineWrapVal = settings.value("log/line_wrap", QPlainTextEdit::LineWrapMode::WidgetWidth).toInt();
    if (lineWrapVal == QPlainTextEdit::LineWrapMode::NoWrap)
    {
        m_TextEdit->setLineWrapMode(QPlainTextEdit::LineWrapMode::NoWrap);
        m_WordWrapAction->setIcon(iconFor("format_align_left"));
        m_WordWrapAction->setToolTip("Enable word wrap");
    }
    else
    {
        m_TextEdit->setLineWrapMode(QPlainTextEdit::LineWrapMode::WidgetWidth);
        m_WordWrapAction->setIcon(iconFor("format_align_justify"));
        m_WordWrapAction->setToolTip("Disable word wrap");
    }

    // setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_TextEdit->setMinimumWidth(600);
    m_TextEdit->setMinimumHeight(100);

    connect(m_TextEdit->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        auto* scrollBar = m_TextEdit->verticalScrollBar();
        m_autoScrollEnabled = (scrollBar->value() >= scrollBar->maximum() - 4);
    });

    m_CanvasLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    m_CanvasLayout->setMenuBar(tb);
    m_CanvasLayout->addWidget(m_TextEdit);

    connect(m_AnsiColorsAction, &QAction::toggled, this, &LogViewerControl::setAnsiColorFormattingEnabled);

    m_MaxLines = settings.value("log/max_lines", 1000).toInt();

    m_CurrentFontSize = m_OriginalFontSize = settings.value("log/font_size", 12).toInt();

    const QFont defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QString ftFamily = settings.value("log/font_family", defaultFont.family()).toString();
    m_FontItalic = settings.value("log/font_italic", false).toBool();
    m_FontBold = settings.value("log/font_bold", false).toBool();
    const ThemeColorDefaults themeDefaults = currentThemeColorDefaults();
    const QString ftColor = settings.value("log/font_color", themeDefaults.fontColor.name(QColor::HexRgb)).toString();
    const QString bgColor =
        settings.value("log/background_color", themeDefaults.backgroundColor.name(QColor::HexRgb)).toString();
    const QString ftHColor =
        settings.value("log/font_highlight_color", themeDefaults.fontHighlightColor.name(QColor::HexRgb)).toString();
    const QString bgHColor =
        settings.value("log/background_highlight_color", themeDefaults.backgroundHighlightColor.name(QColor::HexRgb))
            .toString();

    m_Font = defaultFont;
    m_Font.setFamily(ftFamily);

    m_FontColor = parseColorOrFallback(ftColor, themeDefaults.fontColor);
    m_BackgroundColor = parseColorOrFallback(bgColor, themeDefaults.backgroundColor);
    m_FontHighlightColor = parseColorOrFallback(ftHColor, themeDefaults.fontHighlightColor);
    m_BackgroundHighlightColor = parseColorOrFallback(bgHColor, themeDefaults.backgroundHighlightColor);

    // Apply font + palette without reloading (we call reloadContent below).
    m_updatingSettings = true;
    updateSettings();
    m_updatingSettings = false;
    updateFileLogLevelIndicator();

    if (auto* bridge = an::ui::SessionConsoleBridge::instance())
    {
        connect(bridge, &an::ui::SessionConsoleBridge::sessionEntryAppended, this,
                [this](const QString& text, const int level) {
                    appendSessionEntry(text, static_cast<spdlog::level::level_enum>(level));
                });

        connect(bridge, &an::ui::SessionConsoleBridge::sessionHistoryCleared, this, [this]() {
            if (m_TextEdit)
            {
                m_TextEdit->clear();
            }
            resetSearchCursor();
        });
    }

    reloadContent();
}

void LogViewerControl::shutdown()
{
    if (m_IsShutdown)
    {
        return;
    }
    m_IsShutdown = true;

    if (m_LogViewerSettingsControl)
    {
        m_LogViewerSettingsControl->parentDestroyed();
    }

    if (m_LogViewerFindTextControl)
    {
        m_LogViewerFindTextControl->parentDestroyed();
    }

    saveSettings();
    m_LevelComboBox.reset();
}

void LogViewerControl::setBackgroundColor(const QColor& color)
{
    m_BackgroundColor = color;
    updateSettings();
}

QColor LogViewerControl::getBackgroundColor() const
{
    return m_BackgroundColor;
}

void LogViewerControl::setBackgroundHighlightColor(const QColor& color)
{
    m_BackgroundHighlightColor = color;
    updateSettings();
}

QColor LogViewerControl::getBackgroundHighlightColor() const
{
    return m_BackgroundHighlightColor;
}

void LogViewerControl::setFontColor(const QColor& color)
{
    m_FontColor = color;
    updateSettings();
}

QColor LogViewerControl::getFontColor() const
{
    return m_FontColor;
}

void LogViewerControl::setFontHighlightColor(const QColor& color)
{
    m_FontHighlightColor = color;
    updateSettings();
}

QColor LogViewerControl::getFontHighlightColor() const
{
    return m_FontHighlightColor;
}

void LogViewerControl::setFontSize(const int size)
{
    m_OriginalFontSize = size;
    m_CurrentFontSize = size;
    updateSettings();
}

int LogViewerControl::getOriginalFontSize() const
{
    return m_OriginalFontSize;
}

int LogViewerControl::getCurrentFontSize() const
{
    return m_CurrentFontSize;
}

void LogViewerControl::notifyLogViewerSettingsControlClosed()
{
    m_SettingsAction->setEnabled(true);
    m_LogViewerSettingsControl = nullptr;
}

void LogViewerControl::notifyLogViewerFindTextControlClosed()
{
    resetSearchCursor();
    m_SearchAction->setEnabled(true);
    m_LogViewerFindTextControl = nullptr;
}

void LogViewerControl::beginBatchUpdate()
{
    m_updatingSettings = true;
}

void LogViewerControl::endBatchUpdate()
{
    m_updatingSettings = false;
    updateSettings();
}

void LogViewerControl::setMaxLines(const int maxLines)
{
    m_MaxLines = maxLines;
    if (!m_updatingSettings)
    {
        reloadContent();
    }
}

int LogViewerControl::getMaxLines() const
{
    return m_MaxLines;
}

spdlog::level::level_enum LogViewerControl::currentMinimumLevel() const
{
    if (!m_LevelComboBox)
    {
        return spdlog::level::info;
    }

    const QString text = m_LevelComboBox->currentText();
    if (text == "error")
    {
        return spdlog::level::err;
    }
    if (text == "warn")
    {
        return spdlog::level::warn;
    }
    if (text == "debug")
    {
        return spdlog::level::debug;
    }
    if (text == "trace")
    {
        return spdlog::level::trace;
    }

    return spdlog::level::info;
}

void LogViewerControl::updateFileLogLevelIndicator()
{
    if (!m_FileLevelIndicator)
    {
        return;
    }

    an::platform::LoggingConfig config;
    an::platform::loadLoggingConfig(config);

    const QString level = QString::fromStdString(config.logLevel).trimmed().toLower();
    const QString normalizedLevel = level.isEmpty() ? QStringLiteral("info") : level;
    m_FileLevelIndicator->setText(tr("File log: %1").arg(normalizedLevel));
}

void LogViewerControl::appendSessionEntry(const QString& text, const spdlog::level::level_enum level,
                                          const bool scrollToEnd)
{
    if (!m_TextEdit || level < currentMinimumLevel())
    {
        return;
    }

    QTextCursor cursor(m_TextEdit->document());
    cursor.movePosition(QTextCursor::End);

    // Insert a block separator before the text (except for the first entry)
    // so there is no trailing empty line after the last entry.
    if (!m_TextEdit->document()->isEmpty())
    {
        cursor.insertBlock();
    }

    const int blockStart = cursor.position();
    QTextCharFormat baseFormat;
    baseFormat.setFont(m_TextEdit->font());
    baseFormat.setForeground(m_FontColor);
    baseFormat.setBackground(QBrush(Qt::NoBrush));
    baseFormat.setFontUnderline(false);

    const QString plainText = insertAnsiAwareText(cursor, text, baseFormat, m_FontColor, m_BackgroundColor,
                                                  resolveThemeVariant(), m_AnsiColorFormattingEnabled);

    const auto levelSV = spdlog::level::to_string_view(level);
    const QString levelName = QString::fromUtf8(levelSV.data(), static_cast<int>(levelSV.size()));
    const QString searchToken = QStringLiteral(" - ") + levelName;
    const int tokenIndex = plainText.indexOf(searchToken);
    if (tokenIndex >= 0)
    {
        const int levelStart = tokenIndex + 3; // skip " - "
        cursor.setPosition(blockStart + levelStart);
        cursor.setPosition(blockStart + levelStart + levelName.length(), QTextCursor::KeepAnchor);

        QTextCharFormat levelFormat = cursor.charFormat();
        levelFormat.setForeground(logLevelColor(level));
        cursor.setCharFormat(levelFormat);
    }

    trimVisibleLines();
    if (scrollToEnd && m_autoScrollEnabled)
    {
        scrollToBottom();
    }
}

void LogViewerControl::trimVisibleLines()
{
    if (!m_TextEdit)
    {
        return;
    }

    QTextCursor cursor(m_TextEdit->document());
    while (m_TextEdit->document()->blockCount() > m_MaxLines)
    {
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

void LogViewerControl::scrollToBottom()
{
    if (!m_TextEdit)
    {
        return;
    }

    QScrollBar* const horizontalScrollBar = m_TextEdit->horizontalScrollBar();
    const bool keepHorizontalScrollAtLeft =
        horizontalScrollBar && m_TextEdit->lineWrapMode() == QPlainTextEdit::LineWrapMode::NoWrap;

    QTextCursor cursor = m_TextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_TextEdit->setTextCursor(cursor);
    m_TextEdit->ensureCursorVisible();

    if (keepHorizontalScrollAtLeft)
    {
        horizontalScrollBar->setValue(horizontalScrollBar->minimum());
    }
}

void LogViewerControl::saveSettings()
{
    if (!m_TextEdit)
    {
        return;
    }

    QSettings settings = an::ui::applicationSettings();

    settings.setValue("log/line_wrap", (int)m_TextEdit->lineWrapMode());
    settings.setValue("log/max_lines", m_MaxLines);
    settings.setValue("log/font_size", m_OriginalFontSize);

    settings.setValue("log/font_family", m_Font.family());
    settings.setValue("log/font_italic", m_FontItalic);
    settings.setValue("log/font_bold", m_FontBold);
    settings.setValue("log/font_color", m_FontColor.name());
    settings.setValue("log/background_color", m_BackgroundColor.name());
    settings.setValue("log/font_highlight_color", m_FontHighlightColor.name());
    settings.setValue("log/background_highlight_color", m_BackgroundHighlightColor.name());
    settings.setValue(kAnsiColorFormattingSettingKey, m_AnsiColorFormattingEnabled);
}

void LogViewerControl::setAnsiColorFormattingEnabled(const bool enabled)
{
    if (m_AnsiColorFormattingEnabled == enabled)
    {
        return;
    }

    m_AnsiColorFormattingEnabled = enabled;
    if (m_AnsiColorsAction && m_AnsiColorsAction->isChecked() != enabled)
    {
        m_AnsiColorsAction->setChecked(enabled);
    }

    saveSettings();
    if (!m_updatingSettings)
    {
        reloadContent();
    }
}

void LogViewerControl::findText(const QString& text, bool caseSensitive, bool matchWholeWord, bool regularExpression,
                                bool backward)
{
    if (!m_TextEdit)
    {
        return;
    }

    QTextDocument::FindFlags flags = QTextDocument::FindFlags();
    if (caseSensitive)
    {
        flags |= QTextDocument::FindCaseSensitively;
    }

    if (matchWholeWord)
    {
        flags |= QTextDocument::FindWholeWords;
    }

    if (backward)
    {
        flags |= QTextDocument::FindBackward;
    }

    if (currentSearchCursor.isNull())
    {
        currentSearchCursor = m_TextEdit->textCursor();
    }

    if (regularExpression)
    {
        QRegularExpression::PatternOptions options = QRegularExpression::ExtendedPatternSyntaxOption;
        if (!caseSensitive)
        {
            options |= QRegularExpression::CaseInsensitiveOption;
        }

        QRegularExpression exp(text, options);
        currentSearchCursor = m_TextEdit->document()->find(exp, currentSearchCursor, flags);
        if (currentSearchCursor.isNull())
        {
            // Wrap around the document.
            currentSearchCursor = m_TextEdit->document()->find(exp, 0, flags);
        }
    }
    else
    {
        currentSearchCursor = m_TextEdit->document()->find(text, currentSearchCursor, flags);
        if (currentSearchCursor.isNull())
        {
            // Wrap around the document.
            currentSearchCursor = m_TextEdit->document()->find(text, 0, flags);
        }
    }

    if (!currentSearchCursor.isNull())
    {
        m_TextEdit->setTextCursor(currentSearchCursor);
    }
    else
    {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyInfoMessage(this, tr("Search"), tr("No occurrences found."));
        }
        currentSearchCursor = QTextCursor();
        m_TextEdit->moveCursor(QTextCursor::End);
    }
}

void LogViewerControl::resetSearchCursor()
{
    currentSearchCursor = QTextCursor();
    if (m_TextEdit)
    {
        m_TextEdit->moveCursor(QTextCursor::End);
    }
}

QFont LogViewerControl::getFont() const
{
    return m_Font;
}

LogViewerControl::ThemeColorDefaults LogViewerControl::currentThemeColorDefaults() const
{
    return resolveThemeDefaults();
}

QColor LogViewerControl::logLevelColor(const spdlog::level::level_enum level) const
{
    return semanticLogLevelColor(resolveThemeVariant(), level);
}

void LogViewerControl::setFont(const QFont& font)
{
    m_Font = font;
    updateSettings();
}

void LogViewerControl::setFontItalic(bool italic)
{
    m_FontItalic = italic;
    updateSettings();
}

void LogViewerControl::setFontBold(bool bold)
{
    m_FontBold = bold;
    updateSettings();
}

bool LogViewerControl::getFontItalic() const
{
    return m_FontItalic;
}

bool LogViewerControl::getFontBold() const
{
    return m_FontBold;
}

LogViewerControl::~LogViewerControl()
{
    shutdown();
}

void LogViewerControl::on_level_currentTextChanged(const QString& text)
{
    Q_UNUSED(text);
    reloadContent();
}

void LogViewerControl::closeRequested()
{
    if (m_DockWidget)
    {
        reinterpret_cast<CustomDockWidget*>(m_DockWidget)->forceClose();
    }

    close();
}

void LogViewerControl::isCurrentTabChanged(bool focused)
{
    if (focused)
    {
        updateFileLogLevelIndicator();
    }
    updateTabBarHighlight(focused);
}

void LogViewerControl::isFocusedChanged(bool focused)
{
    if (focused)
    {
        updateFileLogLevelIndicator();
    }
    updateTabBarHighlight(focused);
}

void LogViewerControl::isFloatingChanged(bool floating) {}

void LogViewerControl::guestViewChanged() {}

void LogViewerControl::zoomIn()
{
    m_CurrentFontSize += 1;
    updateSettings();
}

void LogViewerControl::zoomOut()
{
    m_CurrentFontSize -= 1;
    updateSettings();
}

void LogViewerControl::zoom100()
{
    m_CurrentFontSize = m_OriginalFontSize;
    updateSettings();
}

void LogViewerControl::wordWrap()
{
    if (m_TextEdit)
    {
        if (m_TextEdit->lineWrapMode() == QPlainTextEdit::LineWrapMode::NoWrap)
        {
            m_TextEdit->setLineWrapMode(QPlainTextEdit::LineWrapMode::WidgetWidth);
            m_WordWrapAction->setIcon(iconForName("format_align_justify"));
            m_WordWrapAction->setToolTip("Disable word wrap");
        }
        else
        {
            m_TextEdit->setLineWrapMode(QPlainTextEdit::LineWrapMode::NoWrap);
            m_WordWrapAction->setIcon(iconForName("format_align_left"));
            m_WordWrapAction->setToolTip("Enable word wrap");
        }
    }
}

void LogViewerControl::clearContent()
{
    if (auto* bridge = an::ui::SessionConsoleBridge::instance())
    {
        bridge->clearHistory();
    }
    else if (m_TextEdit)
    {
        m_TextEdit->clear();
        resetSearchCursor();
    }
}

void LogViewerControl::reloadContent()
{
    if (!m_TextEdit)
    {
        return;
    }

    updateFileLogLevelIndicator();
    m_TextEdit->clear();
    resetSearchCursor();

    const auto* bridge = an::ui::SessionConsoleBridge::instance();
    if (!bridge)
    {
        return;
    }

    const auto snapshot = bridge->entriesSnapshot();
    for (const auto& entry : snapshot)
    {
        appendSessionEntry(entry.text, entry.level, false);
    }
    scrollToBottom();
}

void LogViewerControl::settings()
{
    m_SettingsAction->setEnabled(false);

    m_LogViewerSettingsControl = new LogViewerSettingsControl(this);

    m_LogViewerSettingsControl->show();
}

void LogViewerControl::search()
{
    m_SearchAction->setEnabled(false);
    m_LogViewerFindTextControl = new LogViewerFindTextControl(this);

    m_LogViewerFindTextControl->show();
}

void LogViewerControl::save()
{
    static const QString contextKey = QStringLiteral("__SESSION_CONSOLE_FOLDER__");
    QString initialDir;
    if (auto* pathMemory = currentPathMemoryService())
    {
        initialDir = pathMemory->selectPath(contextKey);
        if (!initialDir.isEmpty() && !QDir(initialDir).exists())
        {
            initialDir.clear();
        }
    }

    const QString defaultPath = initialDir.isEmpty() ? QStringLiteral("session_console.txt")
                                                     : QDir(initialDir).filePath(QStringLiteral("session_console.txt"));
    QString fileName = an::ui::dialogs::getSaveFileName(
        currentGlobalSettings(), this, tr("Export visible session text..."), defaultPath, tr("Text files (*.txt)"));
    if (fileName.isEmpty())
    {
        return;
    }

    if (!fileName.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
    {
        fileName += QStringLiteral(".txt");
    }

    if (auto* pathMemory = currentPathMemoryService())
    {
        pathMemory->upsertPath(contextKey, QFileInfo(fileName).absolutePath());
    }

    if (!m_TextEdit)
    {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyWarnMessage(this, tr("File Error"), tr("Unable to open file for writing"));
        }
        return;
    }

    QTextStream out(&file);
    out << m_TextEdit->toPlainText();
}

void LogViewerControl::closeEvent(QCloseEvent* event)
{
    if (m_DockWidget)
    {
        auto* dockWidget = m_DockWidget;
        m_DockWidget = nullptr;
        dockWidget->setWidget(nullptr);
        delete dockWidget;
    }

    QWidget::closeEvent(event);
}

void LogViewerControl::updateSettings()
{
    if (m_TextEdit)
    {
        QFont font(m_Font);
        font.setPointSize(m_CurrentFontSize);
        font.setItalic(m_FontItalic);
        font.setBold(m_FontBold);
        m_TextEdit->setFont(font);

        QPalette pal = m_TextEdit->palette();
        pal.setColor(QPalette::Text, m_FontColor);
        pal.setColor(QPalette::Base, m_BackgroundColor);
        pal.setColor(QPalette::HighlightedText, m_FontHighlightColor);
        pal.setColor(QPalette::Highlight, m_BackgroundHighlightColor);
        m_TextEdit->setPalette(pal);

        // Ensure the viewport also picks up the selection palette
        // (QTextEdit sometimes styles its internal viewport separately).
        m_TextEdit->viewport()->setPalette(pal);

        // Re-render entries so per-line level colors are applied on top of the
        // updated font and palette.
        if (!m_updatingSettings)
        {
            reloadContent();
        }
    }
}
