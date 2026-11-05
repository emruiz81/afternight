// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LogViewerSettingsControl.h"
#include "controls/IAppContext.h"

#include <QColorDialog>

static void selectLines(QPlainTextEdit* textEdit, int lineNumber, int numLines)
{
    // Get the document from the text edit
    QTextDocument* document = textEdit->document();

    // Create a text cursor for the document
    QTextCursor cursor(document);

    // Move the cursor to the specified line number
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < lineNumber; ++i)
    {
        cursor.movePosition(QTextCursor::Down);
    }

    // Move the cursor to the start of the first line
    cursor.movePosition(QTextCursor::StartOfLine);

    // Select the specified number of lines
    for (int i = 0; i < numLines - 1; ++i)
    {
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
    }

    // Extend selection to the end of the last line
    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);

    // Set the cursor in the text edit (this will highlight the selected text)
    textEdit->setTextCursor(cursor);
}
//
// static void clearSelections(QPlainTextEdit* textEdit) {
//    // Create a cursor and set it to the start of the document
//    QTextCursor cursor = textEdit->textCursor();
//    cursor.clearSelection();
//    textEdit->setTextCursor(cursor);
//}

static QString getCurrentDateTimeAsString()
{
    // Get current time in milliseconds since the epoch
    qint64 msSinceEpoch = QDateTime::currentMSecsSinceEpoch();

    // Convert to QDateTime
    QDateTime currentDateTime = QDateTime::fromMSecsSinceEpoch(msSinceEpoch);

    // Format the date and time string
    return currentDateTime.toString("yyyy-MM-dd HH:mm:ss.zzz");
}

LogViewerSettingsControl::LogViewerSettingsControl(LogViewerControl* parent)
    : QDialog(parent), m_LogViewerControl(parent)
{
    m_ui = an::ui::controls::buildLogViewerSettingsControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &LogViewerSettingsControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.fontColorPickerButton, &QPushButton::released, this,
                     &LogViewerSettingsControl::on_fontColorPickerButtonPushed);
    QObject::connect(m_ui.backgroundColorPickerButton, &QPushButton::released, this,
                     &LogViewerSettingsControl::on_backgroundColorPickerButtonPushed);
    QObject::connect(m_ui.fontHighlightColorPickerButton, &QPushButton::released, this,
                     &LogViewerSettingsControl::on_fontHighlightColorPickerButtonPushed);
    QObject::connect(m_ui.backgroundHighlightColorPickerButton, &QPushButton::released, this,
                     &LogViewerSettingsControl::on_backgroundHighlightColorPickerButtonPushed);
    QObject::connect(m_ui.defaultFontSizeValue, &QSpinBox::valueChanged, this,
                     &LogViewerSettingsControl::on_defaultFontSizeValueChanged);
    QObject::connect(m_ui.fontSelector, &QFontComboBox::currentFontChanged, this,
                     &LogViewerSettingsControl::on_fontChanged);
    QObject::connect(m_ui.bold, &QCheckBox::checkStateChanged, this, &LogViewerSettingsControl::on_boldClicked);
    QObject::connect(m_ui.italic, &QCheckBox::checkStateChanged, this, &LogViewerSettingsControl::on_italicClicked);

    if (auto* context = an::ui::appContext())
    {
        if (auto iconProvider = context->getIconProvider())
        {
            const QIcon paletteIcon = iconProvider->getIcon("palette");
            m_ui.fontColorPickerButton->setIcon(paletteIcon);
            m_ui.backgroundColorPickerButton->setIcon(paletteIcon);
            m_ui.fontHighlightColorPickerButton->setIcon(paletteIcon);
            m_ui.backgroundHighlightColorPickerButton->setIcon(paletteIcon);
        }
    }

    setWindowTitle("Log viewer settings");

    m_ui.fontColorEdit_1->setReadOnly(true);
    m_ui.backgroundColorEdit_1->setReadOnly(true);
    m_ui.fontColorEdit_1->setAlignment(Qt::AlignCenter);
    m_ui.backgroundColorEdit_1->setAlignment(Qt::AlignCenter);
    m_ui.fontHighlightColorEdit_1->setReadOnly(true);
    m_ui.backgroundHighlightColorEdit_1->setReadOnly(true);
    m_ui.fontHighlightColorEdit_1->setAlignment(Qt::AlignCenter);
    m_ui.backgroundHighlightColorEdit_1->setAlignment(Qt::AlignCenter);

    setFontColor(m_LogViewerControl->getFontColor());
    setBackgroundColor(m_LogViewerControl->getBackgroundColor());
    setFontHighlightColor(m_LogViewerControl->getFontHighlightColor());
    setBackgroundHighlightColor(m_LogViewerControl->getBackgroundHighlightColor());

    m_ui.defaultFontSizeValue->setValue(m_LogViewerControl->getOriginalFontSize());

    m_ui.fontSelector->setCurrentFont(m_LogViewerControl->getFont());
    m_ui.italic->setChecked(m_LogViewerControl->getFontItalic());
    m_ui.bold->setChecked(m_LogViewerControl->getFontBold());

    m_ui.maxLinesValue->setValue(m_LogViewerControl->getMaxLines());

    m_ui.textEdit->setReadOnly(true);
    m_ui.textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_ui.textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_ui.textEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_ui.textEdit->setUndoRedoEnabled(false);
    m_ui.textEdit->setWordWrapMode(QTextOption::NoWrap);

    auto currentDateTime = getCurrentDateTimeAsString();

    // Build sample lines matching the console log format:
    //   datetime - level___ - message

    auto appendSampleLine = [&](spdlog::level::level_enum lvl, const QString& message) {
        const auto sv = spdlog::level::to_string_view(lvl);
        const QString levelName = QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
        const QString paddedLevel = levelName.leftJustified(8);
        const QString line = QString("%1 - %2 - %3").arg(currentDateTime, paddedLevel, message);

        QTextCursor cursor(m_ui.textEdit->document());
        cursor.movePosition(QTextCursor::End);
        if (!m_ui.textEdit->document()->isEmpty())
            cursor.insertBlock();

        const int blockStart = cursor.position();
        cursor.insertText(line);

        const QString searchToken = QStringLiteral(" - ") + levelName;
        const int tokenIndex = line.indexOf(searchToken);
        if (tokenIndex >= 0)
        {
            const int levelStart = tokenIndex + 3;
            cursor.setPosition(blockStart + levelStart);
            cursor.setPosition(blockStart + levelStart + levelName.length(), QTextCursor::KeepAnchor);

            QTextCharFormat fmt;
            fmt.setForeground(m_LogViewerControl->logLevelColor(lvl));
            cursor.setCharFormat(fmt);
        }
    };

    appendSampleLine(spdlog::level::info, QStringLiteral("This is a sample text to test the selected colors."));
    appendSampleLine(spdlog::level::debug, QStringLiteral("This is another sample text to test the selected colors."));
    appendSampleLine(spdlog::level::info,
                     QStringLiteral("This is a highlighted sample text to test the selected colors."));
    appendSampleLine(spdlog::level::warn,
                     QStringLiteral("This is another highlighted sample text to test the selected colors."));
    appendSampleLine(spdlog::level::warn,
                     QStringLiteral("This is a non highlighted sample text to test the selected colors."));
    appendSampleLine(spdlog::level::err,
                     QStringLiteral("This is another non highlighted text to test the selected colors."));

    selectLines(m_ui.textEdit, 2, 2);

    updateSettings();
}

void LogViewerSettingsControl::parentDestroyed()
{
    m_LogViewerControl = nullptr;
    close();
}

LogViewerSettingsControl::~LogViewerSettingsControl()
{
    if (m_LogViewerControl)
    {
        m_LogViewerControl->notifyLogViewerSettingsControlClosed();
    }
}

void LogViewerSettingsControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        applyChanges();
        this->close();
    }
    else if (sb == QDialogButtonBox::Apply)
    {
        applyChanges();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        const auto defaults = m_LogViewerControl->currentThemeColorDefaults();
        setFontColor(defaults.fontColor);
        setBackgroundColor(defaults.backgroundColor);
        setFontHighlightColor(defaults.fontHighlightColor);
        setBackgroundHighlightColor(defaults.backgroundHighlightColor);
        m_ui.defaultFontSizeValue->setValue(12);
        m_ui.maxLinesValue->setValue(1000);
        m_ui.italic->setChecked(false);
        m_ui.bold->setChecked(false);
        m_ui.fontSelector->setCurrentFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        selectLines(m_ui.textEdit, 2, 2);
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

void LogViewerSettingsControl::on_fontColorPickerButtonPushed()
{
    QColorDialog dlg(m_LogViewerControl->getFontColor(), this);

    if (dlg.exec() == QDialog::Accepted)
    {
        QColor selectedColor = dlg.selectedColor();

        setFontColor(selectedColor);
    }
}

void LogViewerSettingsControl::on_backgroundColorPickerButtonPushed()
{
    QColorDialog dlg(m_LogViewerControl->getBackgroundColor(), this);

    if (dlg.exec() == QDialog::Accepted)
    {
        QColor selectedColor = dlg.selectedColor();

        setBackgroundColor(selectedColor);
    }
}

void LogViewerSettingsControl::on_fontHighlightColorPickerButtonPushed()
{
    QColorDialog dlg(m_LogViewerControl->getFontHighlightColor(), this);

    if (dlg.exec() == QDialog::Accepted)
    {
        QColor selectedColor = dlg.selectedColor();

        setFontHighlightColor(selectedColor);
    }
}

void LogViewerSettingsControl::on_backgroundHighlightColorPickerButtonPushed()
{
    QColorDialog dlg(m_LogViewerControl->getBackgroundHighlightColor(), this);

    if (dlg.exec() == QDialog::Accepted)
    {
        QColor selectedColor = dlg.selectedColor();

        setBackgroundHighlightColor(selectedColor);
    }
}

void LogViewerSettingsControl::on_defaultFontSizeValueChanged(int value)
{
    updateSettings();
}

void LogViewerSettingsControl::on_fontChanged(const QFont& font)
{
    updateSettings();
}

void LogViewerSettingsControl::on_boldClicked(Qt::CheckState state)
{
    updateSettings();
}

void LogViewerSettingsControl::on_italicClicked(Qt::CheckState state)
{
    updateSettings();
}

void LogViewerSettingsControl::setFontColor(const QColor& color)
{
    QColor textColor;
    if (color.lightnessF() < 0.5)
        textColor = Qt::white;
    else
        textColor = Qt::black;

    QPalette pal = m_ui.fontColorEdit_1->palette();
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Text, textColor);
    m_ui.fontColorEdit_1->setPalette(pal);
    m_ui.fontColorEdit_1->setText(color.name());

    updateSettings();
}

void LogViewerSettingsControl::setBackgroundColor(const QColor& color)
{
    QColor textColor;
    if (color.lightnessF() < 0.5)
        textColor = Qt::white;
    else
        textColor = Qt::black;

    QPalette pal = m_ui.backgroundColorEdit_1->palette();
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Text, textColor);
    m_ui.backgroundColorEdit_1->setPalette(pal);
    m_ui.backgroundColorEdit_1->setText(color.name());

    updateSettings();
}

void LogViewerSettingsControl::setFontHighlightColor(const QColor& color)
{
    QColor textColor;
    if (color.lightnessF() < 0.5)
        textColor = Qt::white;
    else
        textColor = Qt::black;

    QPalette pal = m_ui.fontHighlightColorEdit_1->palette();
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Text, textColor);
    m_ui.fontHighlightColorEdit_1->setPalette(pal);
    m_ui.fontHighlightColorEdit_1->setText(color.name());

    updateSettings();
}

void LogViewerSettingsControl::setBackgroundHighlightColor(const QColor& color)
{
    QColor textColor;
    if (color.lightnessF() < 0.5)
        textColor = Qt::white;
    else
        textColor = Qt::black;

    QPalette pal = m_ui.backgroundHighlightColorEdit_1->palette();
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Text, textColor);
    m_ui.backgroundHighlightColorEdit_1->setPalette(pal);
    m_ui.backgroundHighlightColorEdit_1->setText(color.name());

    updateSettings();
}

void LogViewerSettingsControl::applyChanges()
{
    m_LogViewerControl->beginBatchUpdate();

    m_LogViewerControl->setFontColor(QColor(m_ui.fontColorEdit_1->text()));
    m_LogViewerControl->setBackgroundColor(QColor(m_ui.backgroundColorEdit_1->text()));
    m_LogViewerControl->setFontHighlightColor(QColor(m_ui.fontHighlightColorEdit_1->text()));
    m_LogViewerControl->setBackgroundHighlightColor(QColor(m_ui.backgroundHighlightColorEdit_1->text()));
    m_LogViewerControl->setFont(m_ui.fontSelector->currentFont());
    m_LogViewerControl->setFontItalic(m_ui.italic->isChecked());
    m_LogViewerControl->setFontBold(m_ui.bold->isChecked());
    if (m_ui.defaultFontSizeValue->value() != m_LogViewerControl->getOriginalFontSize())
    {
        m_LogViewerControl->setFontSize(m_ui.defaultFontSizeValue->value());
    }

    if (m_ui.maxLinesValue->value() != m_LogViewerControl->getMaxLines())
    {
        m_LogViewerControl->setMaxLines(m_ui.maxLinesValue->value());
    }

    m_LogViewerControl->endBatchUpdate();
    m_LogViewerControl->saveSettings();
}

void LogViewerSettingsControl::updateSettings()
{
    QFont font(m_ui.fontSelector->currentFont());
    font.setPointSize(m_ui.defaultFontSizeValue->value());
    font.setItalic(m_ui.italic->isChecked());
    font.setBold(m_ui.bold->isChecked());
    m_ui.textEdit->setFont(font);

    QPalette pal = m_ui.textEdit->palette();
    pal.setColor(QPalette::Text, QColor(m_ui.fontColorEdit_1->text()));
    pal.setColor(QPalette::Base, QColor(m_ui.backgroundColorEdit_1->text()));
    pal.setColor(QPalette::HighlightedText, QColor(m_ui.fontHighlightColorEdit_1->text()));
    pal.setColor(QPalette::Highlight, QColor(m_ui.backgroundHighlightColorEdit_1->text()));
    m_ui.textEdit->setPalette(pal);
}
