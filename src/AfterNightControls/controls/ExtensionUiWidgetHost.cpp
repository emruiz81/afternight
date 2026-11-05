// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ExtensionUiWidgetHost.h"

#include "components/AnButtonGroup.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnInputField.h"
#include "components/AnLayoutUtils.h"
#include "components/AnSectionHeader.h"
#include "components/AnStyledTable.h"
#include "controls/QClickableSlider.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <verdigris/wobjectimpl.h>

namespace
{

using an::ui::extensions::ExtensionUiDescription;
using an::ui::extensions::ExtensionUiPatchOperation;

constexpr const char* kWidgetTypeProperty = "anExtensionUiWidgetType";
constexpr const char* kWidgetIdProperty = "anExtensionUiWidgetId";
constexpr const char* kFieldLabelObjectName = "anExtensionUiFieldLabel";
constexpr const char* kSliderObjectName = "anExtensionUiSlider";
constexpr const char* kSliderSpinBoxObjectName = "anExtensionUiSliderSpinBox";
constexpr const char* kSpinBoxObjectName = "anExtensionUiSpinBox";
constexpr const char* kDoubleSpinBoxObjectName = "anExtensionUiDoubleSpinBox";
constexpr const char* kCheckBoxObjectName = "anExtensionUiCheckBox";
constexpr const char* kComboBoxObjectName = "anExtensionUiComboBox";
constexpr const char* kPickerLineEditObjectName = "anExtensionUiPickerLineEdit";
constexpr const char* kPickerButtonObjectName = "anExtensionUiPickerButton";
constexpr const char* kStatusIconObjectName = "anExtensionUiStatusIcon";
constexpr const char* kStatusTextObjectName = "anExtensionUiStatusText";
constexpr const char* kStatusToneProperty = "anExtensionUiStatusTone";
constexpr const char* kConsoleOutputObjectName = "anExtensionUiConsoleOutput";
constexpr const char* kConsoleValueProperty = "anExtensionUiConsoleValue";
constexpr const char* kConsoleMinLinesProperty = "anExtensionUiConsoleMinLines";
constexpr const char* kConsoleCollapsedWhenEmptyProperty = "anExtensionUiConsoleCollapsedWhenEmpty";
constexpr const char* kConsoleHasContentProperty = "anExtensionUiConsoleHasContent";
constexpr const char* kConfiguredVisibleProperty = "anExtensionUiConfiguredVisible";
constexpr const char* kFieldLabelAlignedWidthProperty = "anExtensionUiAlignedLabelWidth";

struct BuiltNode
{
    QWidget* widget = nullptr;
    QWidget* child_parent = nullptr;
    QLayout* child_layout = nullptr;
    QString widget_type;
};

struct TableColumnDefinition
{
    QString id;
    QString label;
};

struct BuildContext
{
    QHash<QString, QWidget*>& widgets_by_id;
    QHash<QString, QString>& widget_types;
};

struct ConsoleOutputEntry
{
    QString stream;
    QString text;
};

[[nodiscard]] QString readOptionalString(const QCborMap& node, const char* key)
{
    const auto it = node.find(QString::fromLatin1(key));
    return it != node.end() && it->isString() ? it->toString() : QString();
}

[[nodiscard]] bool readOptionalBool(const QCborMap& node, const char* key, const bool default_value)
{
    const auto it = node.find(QString::fromLatin1(key));
    return it != node.end() && it->isBool() ? it->toBool() : default_value;
}

[[nodiscard]] int readOptionalInt(const QCborMap& node, const char* key, const int default_value)
{
    const auto it = node.find(QString::fromLatin1(key));
    if (it == node.end())
    {
        return default_value;
    }
    if (it->isInteger())
    {
        return static_cast<int>(it->toInteger());
    }
    if (it->isDouble())
    {
        return static_cast<int>(std::lround(it->toDouble()));
    }
    return default_value;
}

[[nodiscard]] double readOptionalDouble(const QCborMap& node, const char* key, const double default_value)
{
    const auto it = node.find(QString::fromLatin1(key));
    if (it == node.end())
    {
        return default_value;
    }
    if (it->isDouble())
    {
        return it->toDouble();
    }
    if (it->isInteger())
    {
        return static_cast<double>(it->toInteger());
    }
    return default_value;
}

[[nodiscard]] QCborValue readOptionalValue(const QCborMap& node, const char* key)
{
    const auto it = node.find(QString::fromLatin1(key));
    return it != node.end() ? it.value() : QCborValue();
}

[[nodiscard]] QCborArray readOptionalArray(const QCborMap& node, const char* key)
{
    const auto it = node.find(QString::fromLatin1(key));
    return it != node.end() && it->isArray() ? it->toArray() : QCborArray();
}

[[nodiscard]] QString cborValueToDisplayText(const QCborValue& value)
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
        return QString::number(value.toDouble());
    }

    const QVariant variant = value.toVariant();
    return variant.isValid() ? variant.toString() : QString();
}

[[nodiscard]] QString normalizeLowercase(QString value)
{
    value = value.trimmed().toLower();
    return value;
}

void applyFieldLabelTheme(QLabel* label)
{
    if (!label)
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    label->setFont(token_set.typography.caption);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, token_set.colors.textSecondary);
    label->setPalette(palette);

    const int content_width = label->sizeHint().width();
    const int aligned_width = label->property(kFieldLabelAlignedWidthProperty).toInt();
    if (aligned_width > 0)
    {
        label->setFixedWidth((std::max)(content_width, aligned_width));
        return;
    }

    label->setMinimumWidth(content_width);
    label->setMaximumWidth(QWIDGETSIZE_MAX);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void normalizeLayout(QLayout* layout)
{
    if (!layout)
    {
        return;
    }

    an::ui::normalizeLayoutSpacingToTokens(layout, an::ui::tokens().spacing);
}

QLabel* createFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(kFieldLabelObjectName));
    label->setWordWrap(false);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setText(text.trimmed());
    label->setVisible(!label->text().isEmpty());
    applyFieldLabelTheme(label);
    return label;
}

[[nodiscard]] QString statusToneForText(const QString& text)
{
    const QString lowered = text.trimmed().toLower();
    if (lowered.contains(QStringLiteral("not activated")) || lowered.contains(QStringLiteral("not licensed")) ||
        lowered.contains(QStringLiteral("unactivated")) || lowered.contains(QStringLiteral("unlicensed")) ||
        lowered.contains(QStringLiteral("invalid")) || lowered.contains(QStringLiteral("expired")))
    {
        return QStringLiteral("error");
    }
    if (lowered.contains(QStringLiteral("activated")) || lowered.contains(QStringLiteral("licensed")) ||
        lowered.contains(QStringLiteral("trial")) || lowered.contains(QStringLiteral("valid")))
    {
        return QStringLiteral("success");
    }
    if (lowered.contains(QStringLiteral("unavailable")) || lowered.contains(QStringLiteral("failed")) ||
        lowered.contains(QStringLiteral("missing")) || lowered.contains(QStringLiteral("not reported")))
    {
        return QStringLiteral("warning");
    }
    return QStringLiteral("info");
}

[[nodiscard]] QColor statusToneColor(const QString& tone)
{
    const auto& colors = an::ui::tokens().colors;
    const QString normalized_tone = normalizeLowercase(tone);
    if (normalized_tone == QStringLiteral("success"))
    {
        return colors.statusSuccess;
    }
    if (normalized_tone == QStringLiteral("error"))
    {
        return colors.statusError;
    }
    if (normalized_tone == QStringLiteral("warning"))
    {
        return colors.statusWarning;
    }
    return colors.statusInfo;
}

[[nodiscard]] QPixmap statusIconPixmap(QWidget* widget, const QString& tone)
{
    const int fallback_icon_size = an::ui::tokens().spacing.lg;
    const int icon_size = widget && widget->style()
                              ? widget->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, widget)
                              : fallback_icon_size;
    const int size = (std::max)(fallback_icon_size, icon_size);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = statusToneColor(tone);
    const qreal pen_width = (std::max)(1.0, static_cast<qreal>(size) / 8.0);
    QPen pen(color, pen_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    const qreal inset = static_cast<qreal>(size) / 4.0;
    const QString normalized_tone = normalizeLowercase(tone);
    if (normalized_tone == QStringLiteral("success"))
    {
        QPainterPath path;
        path.moveTo(static_cast<qreal>(size) * 0.24, static_cast<qreal>(size) * 0.54);
        path.lineTo(static_cast<qreal>(size) * 0.43, static_cast<qreal>(size) * 0.72);
        path.lineTo(static_cast<qreal>(size) * 0.76, static_cast<qreal>(size) * 0.32);
        painter.drawPath(path);
        return pixmap;
    }
    if (normalized_tone == QStringLiteral("error"))
    {
        painter.drawLine(QPointF(inset, inset),
                         QPointF(static_cast<qreal>(size) - inset, static_cast<qreal>(size) - inset));
        painter.drawLine(QPointF(static_cast<qreal>(size) - inset, inset),
                         QPointF(inset, static_cast<qreal>(size) - inset));
        return pixmap;
    }

    painter.drawEllipse(
        QRectF(inset, inset, static_cast<qreal>(size) - (inset * 2.0), static_cast<qreal>(size) - (inset * 2.0)));
    return pixmap;
}

void applyStatusWidgetValue(QWidget* widget, const QString& text, QString tone = QString())
{
    if (!widget)
    {
        return;
    }
    tone = normalizeLowercase(tone);
    if (tone.isEmpty() || tone == QStringLiteral("auto"))
    {
        tone = statusToneForText(text);
    }

    widget->setProperty(kStatusToneProperty, tone);
    if (auto* icon_label = widget->findChild<QLabel*>(QString::fromLatin1(kStatusIconObjectName)))
    {
        icon_label->setPixmap(statusIconPixmap(widget, tone));
        icon_label->setVisible(!text.trimmed().isEmpty());
    }
    if (auto* text_label = widget->findChild<QLabel*>(QString::fromLatin1(kStatusTextObjectName)))
    {
        text_label->setText(text);
        text_label->setFont(an::ui::tokens().typography.body);
        QPalette palette = text_label->palette();
        palette.setColor(QPalette::WindowText, an::ui::tokens().colors.textPrimary);
        text_label->setPalette(palette);
    }
}

[[nodiscard]] QString normalizeConsoleStream(QString stream)
{
    stream = normalizeLowercase(stream);
    if (stream == QStringLiteral("err") || stream == QStringLiteral("stderr") || stream == QStringLiteral("error"))
    {
        return QStringLiteral("stderr");
    }
    if (stream == QStringLiteral("warn") || stream == QStringLiteral("warning"))
    {
        return QStringLiteral("warning");
    }
    if (stream == QStringLiteral("success") || stream == QStringLiteral("info"))
    {
        return stream;
    }
    return QStringLiteral("stdout");
}

[[nodiscard]] QColor consoleColorForStream(const QString& stream)
{
    const auto& colors = an::ui::tokens().colors;
    const QString normalized_stream = normalizeConsoleStream(stream);
    if (normalized_stream == QStringLiteral("stderr"))
    {
        return colors.statusError;
    }
    if (normalized_stream == QStringLiteral("warning"))
    {
        return colors.statusWarning;
    }
    if (normalized_stream == QStringLiteral("info"))
    {
        return colors.statusInfo;
    }
    if (normalized_stream == QStringLiteral("success"))
    {
        return colors.statusSuccess;
    }
    return colors.textPrimary;
}

[[nodiscard]] ConsoleOutputEntry consoleEntryFromLine(QString line, const QString& default_stream)
{
    line = line.trimmed();
    QString stream = normalizeConsoleStream(default_stream);
    const struct
    {
        const char* prefix;
        const char* stream;
    } prefixes[] = {{"[stderr]", "stderr"}, {"stderr:", "stderr"}, {"error:", "stderr"},     {"[stdout]", "stdout"},
                    {"stdout:", "stdout"},  {"out:", "stdout"},    {"[warning]", "warning"}, {"warning:", "warning"},
                    {"warn:", "warning"},   {"[info]", "info"},    {"info:", "info"},        {"[success]", "success"},
                    {"success:", "success"}};

    for (const auto& prefix : prefixes)
    {
        const QString marker = QString::fromLatin1(prefix.prefix);
        if (line.startsWith(marker, Qt::CaseInsensitive))
        {
            stream = QString::fromLatin1(prefix.stream);
            line = line.mid(marker.size()).trimmed();
            break;
        }
    }
    return ConsoleOutputEntry{stream, line};
}

[[nodiscard]] std::vector<ConsoleOutputEntry> consoleEntriesFromText(QString text, const QString& default_stream)
{
    std::vector<ConsoleOutputEntry> entries;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar::fromLatin1('\r'), QChar::fromLatin1('\n'));
    const QStringList lines = text.split(QChar::fromLatin1('\n'));
    entries.reserve(static_cast<std::size_t>(lines.size()));
    for (const QString& line : lines)
    {
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        entries.push_back(consoleEntryFromLine(line, default_stream));
    }
    return entries;
}

[[nodiscard]] QString consoleMapText(const QCborMap& map)
{
    for (const QString& key : {QStringLiteral("text"), QStringLiteral("message"), QStringLiteral("line")})
    {
        const QCborValue value = map.value(key);
        if (!value.isUndefined() && !value.isNull())
        {
            return cborValueToDisplayText(value);
        }
    }
    return cborValueToDisplayText(QCborValue(map));
}

[[nodiscard]] std::vector<ConsoleOutputEntry>
consoleEntriesFromValue(const QCborValue& value, const QString& default_stream = QStringLiteral("stdout"))
{
    std::vector<ConsoleOutputEntry> entries;
    if (value.isArray())
    {
        const QCborArray array = value.toArray();
        entries.reserve(static_cast<std::size_t>(array.size()));
        for (const QCborValue& item : array)
        {
            if (item.isMap())
            {
                const QCborMap map = item.toMap();
                const QString stream =
                    normalizeConsoleStream(map.value(QStringLiteral("stream")).toString(default_stream));
                const std::vector<ConsoleOutputEntry> item_entries =
                    consoleEntriesFromText(consoleMapText(map), stream);
                entries.insert(entries.end(), item_entries.cbegin(), item_entries.cend());
                continue;
            }
            const std::vector<ConsoleOutputEntry> item_entries =
                consoleEntriesFromText(cborValueToDisplayText(item), default_stream);
            entries.insert(entries.end(), item_entries.cbegin(), item_entries.cend());
        }
        return entries;
    }
    if (value.isMap())
    {
        const QCborMap map = value.toMap();
        return consoleEntriesFromText(
            consoleMapText(map), normalizeConsoleStream(map.value(QStringLiteral("stream")).toString(default_stream)));
    }
    return consoleEntriesFromText(cborValueToDisplayText(value), default_stream);
}

[[nodiscard]] QCborValue consoleValueFromEntries(const std::vector<ConsoleOutputEntry>& entries)
{
    QCborArray array;
    for (const ConsoleOutputEntry& entry : entries)
    {
        array.push_back(QCborMap{{QStringLiteral("stream"), normalizeConsoleStream(entry.stream)},
                                 {QStringLiteral("text"), entry.text}});
    }
    return QCborValue(array);
}

void applyConsoleOutputTheme(QPlainTextEdit* editor)
{
    if (!editor)
    {
        return;
    }

    const auto& token_set = an::ui::tokens();
    editor->setFont(token_set.typography.monospace);
    QPalette palette = editor->palette();
    palette.setColor(QPalette::Base, token_set.colors.bgInput);
    palette.setColor(QPalette::Text, token_set.colors.textPrimary);
    palette.setColor(QPalette::WindowText, token_set.colors.textPrimary);
    palette.setColor(QPalette::PlaceholderText, token_set.colors.textTertiary);
    palette.setColor(QPalette::Mid, token_set.colors.borderSubtle);
    editor->setPalette(palette);

    const int min_lines = (std::max)(4, editor->property(kConsoleMinLinesProperty).toInt());
    const QFontMetrics metrics(editor->font());
    editor->setMinimumHeight(metrics.lineSpacing() * min_lines + token_set.spacing.md * 2);
}

void applyConsoleCollapsedVisibility(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    const bool configured_visible = widget->property(kConfiguredVisibleProperty).isValid()
                                        ? widget->property(kConfiguredVisibleProperty).toBool()
                                        : true;
    const bool collapse_when_empty = widget->property(kConsoleCollapsedWhenEmptyProperty).toBool();
    const bool has_content = widget->property(kConsoleHasContentProperty).toBool();
    widget->setVisible(configured_visible && (!collapse_when_empty || has_content));
}

void applyConsoleOutputValue(QWidget* widget, const QCborValue& value)
{
    if (!widget)
    {
        return;
    }
    widget->setProperty(kConsoleValueProperty, value.toVariant());

    auto* editor = widget->findChild<QPlainTextEdit*>(QString::fromLatin1(kConsoleOutputObjectName));
    if (!editor)
    {
        return;
    }

    const QSignalBlocker blocker(editor);
    editor->clear();
    applyConsoleOutputTheme(editor);

    const std::vector<ConsoleOutputEntry> entries = consoleEntriesFromValue(value);
    widget->setProperty(kConsoleHasContentProperty, !entries.empty());
    QTextCursor cursor(editor->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat prefix_format;
    prefix_format.setForeground(an::ui::tokens().colors.textSecondary);
    QTextCharFormat body_format;
    bool first = true;
    for (const ConsoleOutputEntry& entry : entries)
    {
        if (!first)
        {
            cursor.insertBlock();
        }
        first = false;

        const QString stream = normalizeConsoleStream(entry.stream);
        body_format.setForeground(consoleColorForStream(stream));
        if (stream != QStringLiteral("stdout"))
        {
            cursor.insertText(QStringLiteral("[%1] ").arg(stream), prefix_format);
        }
        cursor.insertText(entry.text, body_format);
    }
    editor->moveCursor(QTextCursor::End);
    applyConsoleCollapsedVisibility(widget);
}

void appendConsoleOutputValue(QWidget* widget, const QCborValue& value)
{
    if (!widget)
    {
        return;
    }

    std::vector<ConsoleOutputEntry> entries =
        consoleEntriesFromValue(QCborValue::fromVariant(widget->property(kConsoleValueProperty)));
    std::vector<ConsoleOutputEntry> appended = consoleEntriesFromValue(value);
    if (appended.empty())
    {
        return;
    }

    entries.insert(entries.end(), appended.cbegin(), appended.cend());
    constexpr std::size_t kMaxConsoleEntries = 2000;
    if (entries.size() > kMaxConsoleEntries)
    {
        entries.erase(entries.begin(),
                      entries.begin() + static_cast<std::ptrdiff_t>(entries.size() - kMaxConsoleEntries));
    }
    applyConsoleOutputValue(widget, consoleValueFromEntries(entries));
}

QLabel* findFieldLabel(QWidget* widget)
{
    return widget ? widget->findChild<QLabel*>(QString::fromLatin1(kFieldLabelObjectName)) : nullptr;
}

bool updateFieldLabel(QWidget* widget, const QString& text)
{
    QLabel* label = findFieldLabel(widget);
    if (!label)
    {
        return false;
    }

    label->setText(text.trimmed());
    label->setVisible(!label->text().isEmpty());
    applyFieldLabelTheme(label);
    return true;
}

[[nodiscard]] QLabel* findInputFieldLabel(QWidget* widget)
{
    if (!widget)
    {
        return nullptr;
    }

    if (auto* input = dynamic_cast<an::ui::AnInputField*>(widget))
    {
        return input->label();
    }
    return findFieldLabel(widget);
}

[[nodiscard]] bool isFieldTypeAlignableInGrid(const QString& widget_type)
{
    return widget_type == QStringLiteral("spin_int") || widget_type == QStringLiteral("spin_double") ||
           widget_type == QStringLiteral("checkbox") || widget_type == QStringLiteral("combo") ||
           widget_type == QStringLiteral("line_edit") || widget_type == QStringLiteral("file_picker") ||
           widget_type == QStringLiteral("dir_picker") || widget_type == QStringLiteral("status");
}

void alignFieldLabels(const std::vector<QLabel*>& labels)
{
    if (labels.empty())
    {
        return;
    }

    int max_label_width = 0;
    for (QLabel* label : labels)
    {
        if (!label || !label->isVisible())
        {
            continue;
        }
        max_label_width = (std::max)(max_label_width, (std::max)(label->minimumWidth(), label->sizeHint().width()));
    }

    if (max_label_width <= 0)
    {
        return;
    }

    for (QLabel* label : labels)
    {
        if (!label)
        {
            continue;
        }
        label->setProperty(kFieldLabelAlignedWidthProperty, max_label_width);
        label->setFixedWidth(max_label_width);
    }
}

[[nodiscard]] QCheckBox* findCheckbox(QWidget* widget)
{
    if (!widget)
    {
        return nullptr;
    }
    if (auto* checkbox = dynamic_cast<QCheckBox*>(widget))
    {
        return checkbox;
    }
    return widget->findChild<QCheckBox*>(QString::fromLatin1(kCheckBoxObjectName));
}

[[nodiscard]] const QCheckBox* findCheckbox(const QWidget* widget)
{
    if (!widget)
    {
        return nullptr;
    }
    if (const auto* checkbox = dynamic_cast<const QCheckBox*>(widget))
    {
        return checkbox;
    }
    return widget->findChild<QCheckBox*>(QString::fromLatin1(kCheckBoxObjectName));
}

[[nodiscard]] int decimalsForStep(const double step)
{
    if (!(step > 0.0) || !std::isfinite(step))
    {
        return 3;
    }

    double remaining = step;
    int decimals = 0;
    while (decimals < 6 && std::fabs(remaining - std::round(remaining)) > 1e-8)
    {
        remaining *= 10.0;
        ++decimals;
    }

    return decimals;
}

void storeSliderMetadata(QWidget* widget, const double minimum, const double maximum, const double step)
{
    if (!widget)
    {
        return;
    }

    const double bounded_min = (std::min)(minimum, maximum);
    const double bounded_max = (std::max)(minimum, maximum);
    const double bounded_step = step > 0.0 ? step : 0.01;
    const double span = (std::max)(0.0, bounded_max - bounded_min);
    const int steps = span <= 0.0 ? 1 : std::clamp(static_cast<int>(std::lround(span / bounded_step)), 1, 100000);

    widget->setProperty("anExtensionUiSliderMin", bounded_min);
    widget->setProperty("anExtensionUiSliderMax", bounded_max);
    widget->setProperty("anExtensionUiSliderStep", bounded_step);
    widget->setProperty("anExtensionUiSliderSteps", steps);
}

[[nodiscard]] double sliderMinimum(const QWidget* widget)
{
    return widget ? widget->property("anExtensionUiSliderMin").toDouble() : 0.0;
}

[[nodiscard]] double sliderMaximum(const QWidget* widget)
{
    return widget ? widget->property("anExtensionUiSliderMax").toDouble() : 1.0;
}

[[nodiscard]] double sliderStep(const QWidget* widget)
{
    const double value = widget ? widget->property("anExtensionUiSliderStep").toDouble() : 0.01;
    return value > 0.0 ? value : 0.01;
}

[[nodiscard]] int sliderSteps(const QWidget* widget)
{
    const int value = widget ? widget->property("anExtensionUiSliderSteps").toInt() : 1;
    return (std::max)(1, value);
}

void configureSliderWidgets(QWidget* widget, const double minimum, const double maximum, const double step)
{
    if (!widget)
    {
        return;
    }

    auto* slider = widget->findChild<QSlider*>(QString::fromLatin1(kSliderObjectName));
    auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName));
    if (!slider || !spin_box)
    {
        return;
    }

    storeSliderMetadata(widget, minimum, maximum, step);
    const double bounded_min = sliderMinimum(widget);
    const double bounded_max = sliderMaximum(widget);
    const double bounded_step = sliderStep(widget);
    const int steps = sliderSteps(widget);

    slider->setRange(0, steps);
    spin_box->setDecimals(decimalsForStep(bounded_step));
    spin_box->setRange(bounded_min, bounded_max);
    spin_box->setSingleStep(bounded_step);
}

[[nodiscard]] int sliderPositionForValue(const QWidget* widget, const double value)
{
    const double minimum = sliderMinimum(widget);
    const double maximum = sliderMaximum(widget);
    const double bounded = std::clamp(value, minimum, maximum);
    const double span = (std::max)(0.0, maximum - minimum);
    if (span <= 0.0)
    {
        return 0;
    }

    const double ratio = (bounded - minimum) / span;
    return std::clamp(static_cast<int>(std::lround(ratio * sliderSteps(widget))), 0, sliderSteps(widget));
}

[[nodiscard]] double sliderValueForPosition(const QWidget* widget, const int position)
{
    const double minimum = sliderMinimum(widget);
    const double maximum = sliderMaximum(widget);
    const double span = (std::max)(0.0, maximum - minimum);
    if (span <= 0.0)
    {
        return minimum;
    }

    const int steps = sliderSteps(widget);
    const double ratio = static_cast<double>(std::clamp(position, 0, steps)) / static_cast<double>(steps);
    const double raw_value = minimum + span * ratio;
    const double step = sliderStep(widget);
    const double snapped = minimum + std::round((raw_value - minimum) / step) * step;
    return std::clamp(snapped, minimum, maximum);
}

void setSliderValue(QWidget* widget, const double value)
{
    if (!widget)
    {
        return;
    }

    auto* slider = widget->findChild<QSlider*>(QString::fromLatin1(kSliderObjectName));
    auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName));
    if (!slider || !spin_box)
    {
        return;
    }

    const double bounded = std::clamp(value, sliderMinimum(widget), sliderMaximum(widget));
    const QSignalBlocker slider_blocker(slider);
    const QSignalBlocker spin_blocker(spin_box);
    slider->setValue(sliderPositionForValue(widget, bounded));
    spin_box->setValue(bounded);
}

bool extractOptionEntries(const QCborArray& options, std::vector<std::pair<QString, QVariant>>* out_entries,
                          std::string* error)
{
    if (!out_entries)
    {
        if (error)
        {
            *error = "Option output container is null";
        }
        return false;
    }

    out_entries->clear();
    out_entries->reserve(static_cast<std::size_t>(options.size()));
    for (const QCborValue& option_value : options)
    {
        if (option_value.isArray())
        {
            const QCborArray option_array = option_value.toArray();
            if (option_array.size() < 2 || !option_array.at(0).isString())
            {
                if (error)
                {
                    *error = "Option arrays must contain a string label and a value";
                }
                return false;
            }

            out_entries->push_back({option_array.at(0).toString(), option_array.at(1).toVariant()});
            continue;
        }

        if (option_value.isString())
        {
            const QString label = option_value.toString();
            out_entries->push_back({label, label});
            continue;
        }

        if (error)
        {
            *error = "Options must be strings or [label, value] arrays";
        }
        return false;
    }

    return true;
}

bool populateComboBox(QComboBox* combo_box, const QCborArray& options, const QCborValue& current_value,
                      std::string* error)
{
    if (!combo_box)
    {
        if (error)
        {
            *error = "Combo box is unavailable";
        }
        return false;
    }

    std::vector<std::pair<QString, QVariant>> entries;
    if (!extractOptionEntries(options, &entries, error))
    {
        return false;
    }

    const QVariant target_value = current_value.toVariant();
    const QSignalBlocker blocker(combo_box);
    combo_box->clear();
    int selected_index = -1;
    for (const auto& [label, value] : entries)
    {
        combo_box->addItem(label, value);
        if (selected_index < 0 && value == target_value)
        {
            selected_index = combo_box->count() - 1;
        }
    }
    if (selected_index >= 0)
    {
        combo_box->setCurrentIndex(selected_index);
    }

    return true;
}

bool selectComboValue(QComboBox* combo_box, const QCborValue& value, std::string* error)
{
    if (!combo_box)
    {
        if (error)
        {
            *error = "Combo box is unavailable";
        }
        return false;
    }

    const QVariant target_value = value.toVariant();
    for (int index = 0; index < combo_box->count(); ++index)
    {
        if (combo_box->itemData(index) == target_value)
        {
            combo_box->setCurrentIndex(index);
            return true;
        }
    }

    if (error)
    {
        *error = "Combo value does not match any configured option";
    }
    return false;
}

bool populateButtonGroup(an::ui::AnButtonGroup* button_group, const QCborArray& options,
                         const QCborValue& current_value, std::string* error)
{
    if (!button_group)
    {
        if (error)
        {
            *error = "Button group is unavailable";
        }
        return false;
    }

    std::vector<std::pair<QString, QVariant>> entries;
    if (!extractOptionEntries(options, &entries, error))
    {
        return false;
    }

    button_group->clearButtons();
    const QVariant selected_value = current_value.toVariant();
    for (const auto& [label, value] : entries)
    {
        QPushButton* button = button_group->addButton(label);
        if (!button)
        {
            continue;
        }

        button->setProperty("anExtensionUiValue", value);
        button_group->setButtonVariant(button, value == selected_value
                                                   ? an::ui::AnButtonGroup::ButtonVariant::Primary
                                                   : an::ui::AnButtonGroup::ButtonVariant::Secondary);
    }

    return true;
}

bool selectButtonGroupValue(an::ui::AnButtonGroup* button_group, const QCborValue& value, std::string* error)
{
    if (!button_group)
    {
        if (error)
        {
            *error = "Button group is unavailable";
        }
        return false;
    }

    const QVariant selected_value = value.toVariant();
    bool matched = false;
    for (QPushButton* button : button_group->buttons())
    {
        const bool is_selected = button && button->property("anExtensionUiValue") == selected_value;
        if (button)
        {
            button_group->setButtonVariant(button, is_selected ? an::ui::AnButtonGroup::ButtonVariant::Primary
                                                               : an::ui::AnButtonGroup::ButtonVariant::Secondary);
        }
        matched = matched || is_selected;
    }

    if (!matched && error)
    {
        *error = "Button-group value does not match any configured option";
    }
    return matched;
}

bool parseTableColumns(const QCborArray& columns, std::vector<TableColumnDefinition>* out_columns, std::string* error)
{
    if (!out_columns)
    {
        if (error)
        {
            *error = "Table column output is null";
        }
        return false;
    }

    out_columns->clear();
    out_columns->reserve(static_cast<std::size_t>(columns.size()));
    for (const QCborValue& column_value : columns)
    {
        TableColumnDefinition column;
        if (column_value.isString())
        {
            column.label = column_value.toString();
            column.id = column.label;
        }
        else if (column_value.isMap())
        {
            const QCborMap column_map = column_value.toMap();
            column.label = readOptionalString(column_map, "label");
            column.id = readOptionalString(column_map, "id");
            if (column.id.isEmpty())
            {
                column.id = column.label;
            }
        }
        else
        {
            if (error)
            {
                *error = "Table columns must be strings or maps";
            }
            return false;
        }

        if (column.label.isEmpty())
        {
            column.label = column.id;
        }
        out_columns->push_back(std::move(column));
    }

    return true;
}

bool populateTable(an::ui::AnStyledTable* table, const QCborArray& columns, const QCborArray& rows, std::string* error)
{
    if (!table)
    {
        if (error)
        {
            *error = "Table widget is unavailable";
        }
        return false;
    }

    std::vector<TableColumnDefinition> parsed_columns;
    if (!parseTableColumns(columns, &parsed_columns, error))
    {
        return false;
    }

    int inferred_column_count = static_cast<int>(parsed_columns.size());
    for (const QCborValue& row_value : rows)
    {
        if (row_value.isArray())
        {
            inferred_column_count = (std::max)(inferred_column_count, static_cast<int>(row_value.toArray().size()));
        }
    }

    table->clearContents();
    table->setRowCount(rows.size());
    table->setColumnCount(inferred_column_count);

    QStringList headers;
    headers.reserve(inferred_column_count);
    for (int column_index = 0; column_index < inferred_column_count; ++column_index)
    {
        if (column_index < static_cast<int>(parsed_columns.size()))
        {
            headers.push_back(parsed_columns[static_cast<std::size_t>(column_index)].label);
        }
        else
        {
            headers.push_back(QStringLiteral("Column %1").arg(column_index + 1));
        }
    }
    table->setHorizontalHeaderLabels(headers);

    for (int row_index = 0; row_index < rows.size(); ++row_index)
    {
        const QCborValue row_value = rows.at(row_index);
        if (row_value.isArray())
        {
            const QCborArray row_array = row_value.toArray();
            const int row_column_count = (std::min)(static_cast<int>(row_array.size()), inferred_column_count);
            for (int column_index = 0; column_index < row_column_count; ++column_index)
            {
                table->setItem(row_index, column_index,
                               new QTableWidgetItem(cborValueToDisplayText(row_array.at(column_index))));
            }
            continue;
        }

        if (row_value.isMap())
        {
            if (parsed_columns.empty())
            {
                if (error)
                {
                    *error = "Table rows as maps require explicit columns";
                }
                return false;
            }

            const QCborMap row_map = row_value.toMap();
            for (int column_index = 0; column_index < inferred_column_count; ++column_index)
            {
                if (column_index >= static_cast<int>(parsed_columns.size()))
                {
                    continue;
                }

                const auto& column = parsed_columns[static_cast<std::size_t>(column_index)];
                const auto it = row_map.find(column.id);
                if (it == row_map.end())
                {
                    continue;
                }
                table->setItem(row_index, column_index, new QTableWidgetItem(cborValueToDisplayText(it.value())));
            }
            continue;
        }

        if (error)
        {
            *error = "Table rows must be arrays or maps";
        }
        return false;
    }

    table->resizeColumnsToContents();
    return true;
}

QCborArray currentTableColumns(const an::ui::AnStyledTable* table)
{
    QCborArray columns;
    if (!table)
    {
        return columns;
    }

    for (int column_index = 0; column_index < table->columnCount(); ++column_index)
    {
        if (auto* header_item = table->horizontalHeaderItem(column_index))
        {
            columns.push_back(header_item->text());
        }
        else
        {
            columns.push_back(QStringLiteral("Column %1").arg(column_index + 1));
        }
    }

    return columns;
}

QCborArray currentTableRows(const an::ui::AnStyledTable* table)
{
    QCborArray rows;
    if (!table)
    {
        return rows;
    }

    for (int row_index = 0; row_index < table->rowCount(); ++row_index)
    {
        QCborArray row;
        for (int column_index = 0; column_index < table->columnCount(); ++column_index)
        {
            if (auto* item = table->item(row_index, column_index))
            {
                row.push_back(item->text());
            }
            else
            {
                row.push_back(QString());
            }
        }
        rows.push_back(row);
    }

    return rows;
}

[[nodiscard]] bool widgetTypeProducesValidationParam(const QString& widget_type) noexcept
{
    return widget_type == QStringLiteral("slider") || widget_type == QStringLiteral("spin_int") ||
           widget_type == QStringLiteral("spin_double") || widget_type == QStringLiteral("checkbox") ||
           widget_type == QStringLiteral("combo") || widget_type == QStringLiteral("line_edit") ||
           widget_type == QStringLiteral("file_picker") || widget_type == QStringLiteral("dir_picker") ||
           widget_type == QStringLiteral("status") || widget_type == QStringLiteral("collapsible_section") ||
           widget_type == QStringLiteral("button_group");
}

bool readValidationSnapshotValue(const QWidget* widget, const QString& widget_type, QCborValue* out_value,
                                 std::string* error)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Validation snapshot output is null";
        }
        return false;
    }
    if (!widgetTypeProducesValidationParam(widget_type))
    {
        return false;
    }
    if (!widget)
    {
        if (error)
        {
            *error = "Validation snapshot widget is null";
        }
        return false;
    }

    if (widget_type == QStringLiteral("slider"))
    {
        const auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName));
        if (!spin_box)
        {
            if (error)
            {
                *error = "slider control is missing its spin box";
            }
            return false;
        }
        *out_value = QCborValue(spin_box->value());
        return true;
    }
    if (widget_type == QStringLiteral("spin_int"))
    {
        const auto* spin_box = widget->findChild<QSpinBox*>(QString::fromLatin1(kSpinBoxObjectName));
        if (!spin_box)
        {
            if (error)
            {
                *error = "spin_int control is missing its spin box";
            }
            return false;
        }
        *out_value = QCborValue(spin_box->value());
        return true;
    }
    if (widget_type == QStringLiteral("spin_double"))
    {
        const auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kDoubleSpinBoxObjectName));
        if (!spin_box)
        {
            if (error)
            {
                *error = "spin_double control is missing its spin box";
            }
            return false;
        }
        *out_value = QCborValue(spin_box->value());
        return true;
    }
    if (widget_type == QStringLiteral("checkbox"))
    {
        const auto* checkbox = findCheckbox(widget);
        if (!checkbox)
        {
            if (error)
            {
                *error = "checkbox control is unavailable";
            }
            return false;
        }
        *out_value = QCborValue(checkbox->isChecked());
        return true;
    }
    if (widget_type == QStringLiteral("combo"))
    {
        const auto* combo_box = widget->findChild<QComboBox*>(QString::fromLatin1(kComboBoxObjectName));
        if (!combo_box)
        {
            if (error)
            {
                *error = "combo control is missing its combo box";
            }
            return false;
        }
        const int index = combo_box->currentIndex();
        const QVariant value = index >= 0 ? combo_box->itemData(index) : QVariant(combo_box->currentText());
        *out_value = QCborValue::fromVariant(value);
        return true;
    }
    if (widget_type == QStringLiteral("line_edit"))
    {
        const auto* input = dynamic_cast<const an::ui::AnInputField*>(widget);
        if (!input)
        {
            if (error)
            {
                *error = "line_edit control is unavailable";
            }
            return false;
        }
        *out_value = QCborValue(input->text());
        return true;
    }
    if (widget_type == QStringLiteral("file_picker") || widget_type == QStringLiteral("dir_picker"))
    {
        const auto* line_edit = widget->findChild<QLineEdit*>(QString::fromLatin1(kPickerLineEditObjectName));
        if (!line_edit)
        {
            if (error)
            {
                *error = "picker control is missing its line edit";
            }
            return false;
        }
        *out_value = QCborValue(line_edit->text());
        return true;
    }
    if (widget_type == QStringLiteral("status"))
    {
        const auto* text_label = widget->findChild<QLabel*>(QString::fromLatin1(kStatusTextObjectName));
        if (!text_label)
        {
            if (error)
            {
                *error = "status control is missing its text label";
            }
            return false;
        }
        *out_value = QCborValue(text_label->text());
        return true;
    }
    if (widget_type == QStringLiteral("collapsible_section"))
    {
        const auto* section = dynamic_cast<const an::ui::AnCollapsibleSection*>(widget);
        if (!section)
        {
            if (error)
            {
                *error = "collapsible_section control is unavailable";
            }
            return false;
        }
        *out_value = QCborValue(section->isExpanded());
        return true;
    }
    if (widget_type == QStringLiteral("button_group"))
    {
        const auto* button_group = dynamic_cast<const an::ui::AnButtonGroup*>(widget);
        if (!button_group)
        {
            if (error)
            {
                *error = "button_group control is unavailable";
            }
            return false;
        }

        for (QPushButton* button : button_group->buttons())
        {
            if (!button)
            {
                continue;
            }
            if (button_group->buttonVariant(button) == an::ui::AnButtonGroup::ButtonVariant::Primary)
            {
                *out_value = QCborValue::fromVariant(button->property("anExtensionUiValue"));
                return true;
            }
        }

        if (error)
        {
            *error = "button_group control has no selected value";
        }
        return false;
    }

    return false;
}

an::ui::AnInfoBanner::Tone bannerToneFromNode(const QCborMap& node)
{
    const QString tone = normalizeLowercase(readOptionalString(node, "tone"));
    if (tone == QStringLiteral("warning"))
    {
        return an::ui::AnInfoBanner::Tone::Warning;
    }
    if (tone == QStringLiteral("error"))
    {
        return an::ui::AnInfoBanner::Tone::Error;
    }
    return an::ui::AnInfoBanner::Tone::Info;
}

bool setBannerTone(an::ui::AnInfoBanner* banner, const QCborValue& value, std::string* error)
{
    if (!banner)
    {
        if (error)
        {
            *error = "Info banner is unavailable";
        }
        return false;
    }
    if (!value.isString())
    {
        if (error)
        {
            *error = "Banner tone must be a string";
        }
        return false;
    }

    const QString tone = normalizeLowercase(value.toString());
    if (tone == QStringLiteral("warning"))
    {
        banner->setTone(an::ui::AnInfoBanner::Tone::Warning);
        return true;
    }
    if (tone == QStringLiteral("error"))
    {
        banner->setTone(an::ui::AnInfoBanner::Tone::Error);
        return true;
    }
    if (tone == QStringLiteral("info"))
    {
        banner->setTone(an::ui::AnInfoBanner::Tone::Info);
        return true;
    }

    if (error)
    {
        *error = "Banner tone must be one of info, warning, or error";
    }
    return false;
}

[[nodiscard]] bool isContainerWidgetType(const QString& widget_type) noexcept
{
    return widget_type == QStringLiteral("column") || widget_type == QStringLiteral("collapsible_section") ||
           widget_type == QStringLiteral("card");
}

BuiltNode buildColumnWidget(QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("column");

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(layout);

    node.child_parent = node.widget;
    node.child_layout = layout;
    return node;
}

BuiltNode buildSliderWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("slider");

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget));

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    layout->addLayout(row_layout);
    normalizeLayout(row_layout);

    auto* slider = new QClickableSlider(Qt::Horizontal, node.widget);
    slider->setObjectName(QString::fromLatin1(kSliderObjectName));
    row_layout->addWidget(slider, 1);

    auto* spin_box = new QDoubleSpinBox(node.widget);
    spin_box->setObjectName(QString::fromLatin1(kSliderSpinBoxObjectName));
    row_layout->addWidget(spin_box);

    const double minimum = readOptionalDouble(node_map, "min", 0.0);
    const double maximum = readOptionalDouble(node_map, "max", 1.0);
    const double step = readOptionalDouble(node_map, "step", 0.01);
    configureSliderWidgets(node.widget, minimum, maximum, step);
    setSliderValue(node.widget, readOptionalDouble(node_map, "value", minimum));

    QObject::connect(slider, &QSlider::valueChanged, node.widget, [widget = node.widget, spin_box](const int position) {
        if (!widget || !spin_box)
        {
            return;
        }

        const double value = sliderValueForPosition(widget, position);
        const QSignalBlocker blocker(spin_box);
        spin_box->setValue(value);
    });
    QObject::connect(spin_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), node.widget,
                     [widget = node.widget, slider](const double value) {
                         if (!widget || !slider)
                         {
                             return;
                         }

                         const QSignalBlocker blocker(slider);
                         slider->setValue(sliderPositionForValue(widget, value));
                     });

    return node;
}

BuiltNode buildIntSpinWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("spin_int");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);
    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* spin_box = new QSpinBox(node.widget);
    spin_box->setObjectName(QString::fromLatin1(kSpinBoxObjectName));
    spin_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spin_box->setRange(readOptionalInt(node_map, "min", 0), readOptionalInt(node_map, "max", 100));
    spin_box->setSingleStep((std::max)(1, readOptionalInt(node_map, "step", 1)));
    spin_box->setValue(readOptionalInt(node_map, "value", spin_box->minimum()));
    row_layout->addWidget(spin_box, 1);
    return node;
}

BuiltNode buildDoubleSpinWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("spin_double");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);
    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* spin_box = new QDoubleSpinBox(node.widget);
    spin_box->setObjectName(QString::fromLatin1(kDoubleSpinBoxObjectName));
    spin_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const double step = readOptionalDouble(node_map, "step", 0.01);
    spin_box->setDecimals(decimalsForStep(step));
    spin_box->setRange(readOptionalDouble(node_map, "min", 0.0), readOptionalDouble(node_map, "max", 1.0));
    spin_box->setSingleStep(step > 0.0 ? step : 0.01);
    spin_box->setValue(readOptionalDouble(node_map, "value", spin_box->minimum()));
    row_layout->addWidget(spin_box, 1);
    return node;
}

BuiltNode buildCheckboxWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("checkbox");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);
    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* checkbox = new QCheckBox(node.widget);
    checkbox->setObjectName(QString::fromLatin1(kCheckBoxObjectName));
    checkbox->setChecked(readOptionalBool(node_map, "value", false));
    row_layout->addWidget(checkbox, 0, Qt::AlignLeft | Qt::AlignVCenter);
    row_layout->addStretch(1);
    return node;
}

BuiltNode buildComboWidget(const QCborMap& node_map, QWidget* parent, std::string* error)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("combo");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);
    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* combo_box = new QComboBox(node.widget);
    combo_box->setObjectName(QString::fromLatin1(kComboBoxObjectName));
    combo_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row_layout->addWidget(combo_box, 1);
    if (!populateComboBox(combo_box, readOptionalArray(node_map, "options"), readOptionalValue(node_map, "value"),
                          error))
    {
        delete node.widget;
        node.widget = nullptr;
        return node;
    }
    return node;
}

BuiltNode buildLineEditWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    auto* input = new an::ui::AnInputField(readOptionalString(node_map, "label"),
                                           readOptionalString(node_map, "placeholder"), parent);
    input->setText(readOptionalString(node_map, "value"));
    if (readOptionalString(node_map, "echo_mode").trimmed().compare(QStringLiteral("password"), Qt::CaseInsensitive) ==
        0)
    {
        input->lineEdit()->setEchoMode(QLineEdit::Password);
    }
    node.widget = input;
    node.widget_type = QStringLiteral("line_edit");
    return node;
}

BuiltNode buildPickerWidget(const QCborMap& node_map, QWidget* parent, const QString& widget_type)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = widget_type;
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);

    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* line_edit = new QLineEdit(node.widget);
    line_edit->setObjectName(QString::fromLatin1(kPickerLineEditObjectName));
    line_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    line_edit->setPlaceholderText(readOptionalString(node_map, "placeholder"));
    line_edit->setText(readOptionalString(node_map, "value"));
    line_edit->setReadOnly(readOptionalBool(node_map, "read_only", false));
    row_layout->addWidget(line_edit, 1);

    QString button_text = readOptionalString(node_map, "button_label");
    if (button_text.trimmed().isEmpty())
    {
        button_text = widget_type == QStringLiteral("dir_picker") ? QStringLiteral("Select Folder")
                                                                  : QStringLiteral("Select File");
    }
    auto* button = new QPushButton(button_text, node.widget);
    button->setObjectName(QString::fromLatin1(kPickerButtonObjectName));
    row_layout->addWidget(button);
    return node;
}

BuiltNode buildStatusWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("status");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    auto* row_layout = new QHBoxLayout();
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(row_layout);
    layout->addLayout(row_layout);

    row_layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0,
                          Qt::AlignLeft | Qt::AlignVCenter);

    auto* icon_label = new QLabel(node.widget);
    icon_label->setObjectName(QString::fromLatin1(kStatusIconObjectName));
    icon_label->setAlignment(Qt::AlignCenter);
    row_layout->addWidget(icon_label, 0, Qt::AlignVCenter);

    auto* text_label = new QLabel(node.widget);
    text_label->setObjectName(QString::fromLatin1(kStatusTextObjectName));
    text_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row_layout->addWidget(text_label, 1, Qt::AlignVCenter);

    applyStatusWidgetValue(node.widget, readOptionalString(node_map, "value"), readOptionalString(node_map, "tone"));
    return node;
}

BuiltNode buildConsoleOutputWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget = new QWidget(parent);
    node.widget_type = QStringLiteral("console_output");
    node.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(node.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.xs);
    normalizeLayout(layout);

    layout->addWidget(createFieldLabel(readOptionalString(node_map, "label"), node.widget), 0, Qt::AlignLeft);

    auto* editor = new QPlainTextEdit(node.widget);
    editor->setObjectName(QString::fromLatin1(kConsoleOutputObjectName));
    editor->setReadOnly(true);
    editor->setUndoRedoEnabled(false);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    const QString placeholder = readOptionalString(node_map, "placeholder");
    if (!placeholder.isEmpty())
    {
        editor->setPlaceholderText(placeholder);
    }
    editor->setProperty(kConsoleMinLinesProperty, (std::max)(4, readOptionalInt(node_map, "min_lines", 12)));
    node.widget->setProperty(kConsoleCollapsedWhenEmptyProperty,
                             readOptionalBool(node_map, "collapsed_when_empty", false));
    applyConsoleOutputTheme(editor);
    layout->addWidget(editor);

    applyConsoleOutputValue(node.widget, readOptionalValue(node_map, "value"));
    return node;
}

BuiltNode buildSectionHeaderWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    node.widget =
        new an::ui::AnSectionHeader(readOptionalString(node_map, "label"), readOptionalString(node_map, "description"),
                                    readOptionalString(node_map, "action_text"), parent);
    node.widget_type = QStringLiteral("section_header");
    return node;
}

BuiltNode buildCollapsibleSectionWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    auto* section = new an::ui::AnCollapsibleSection(readOptionalString(node_map, "label"), parent);
    section->setExpanded(readOptionalBool(node_map, "expanded", true));

    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(layout);
    section->setContentLayout(layout);

    node.widget = section;
    node.child_parent = section->contentWidget();
    node.child_layout = layout;
    node.widget_type = QStringLiteral("collapsible_section");
    return node;
}

BuiltNode buildInfoBannerWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    const QString message = readOptionalString(node_map, "message").isEmpty() ? readOptionalString(node_map, "label")
                                                                              : readOptionalString(node_map, "message");
    node.widget = new an::ui::AnInfoBanner(message, bannerToneFromNode(node_map), parent);
    node.widget_type = QStringLiteral("info_banner");
    return node;
}

BuiltNode buildCardWidget(const QCborMap& node_map, QWidget* parent)
{
    BuiltNode node;
    auto* card = new an::ui::AnCardWidget(readOptionalString(node_map, "label"), parent);
    card->setProperty("anCardCompact", readOptionalBool(node_map, "compact", false));
    card->setProperty("anCardBodyPadded", readOptionalBool(node_map, "body_padded", false));
    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);
    normalizeLayout(layout);
    card->setContentLayout(layout);
    card->refreshTheme();

    node.widget = card;
    node.child_parent = card->contentWidget();
    node.child_layout = layout;
    node.widget_type = QStringLiteral("card");
    return node;
}

BuiltNode buildButtonGroupWidget(const QCborMap& node_map, QWidget* parent, std::string* error)
{
    BuiltNode node;
    auto* button_group = new an::ui::AnButtonGroup(parent);
    if (!populateButtonGroup(button_group, readOptionalArray(node_map, "options"), readOptionalValue(node_map, "value"),
                             error))
    {
        delete button_group;
        return node;
    }

    node.widget = button_group;
    node.widget_type = QStringLiteral("button_group");
    return node;
}

BuiltNode buildTableWidget(const QCborMap& node_map, QWidget* parent, std::string* error)
{
    BuiltNode node;
    auto* table = new an::ui::AnStyledTable(parent);
    if (!populateTable(table, readOptionalArray(node_map, "columns"), readOptionalArray(node_map, "rows"), error))
    {
        delete table;
        return node;
    }

    node.widget = table;
    node.widget_type = QStringLiteral("table");
    return node;
}

void applyCommonNodeProperties(QWidget* widget, const QCborMap& node_map)
{
    if (!widget)
    {
        return;
    }

    const QString tooltip = readOptionalString(node_map, "tooltip");
    if (!tooltip.isEmpty())
    {
        widget->setToolTip(tooltip);
    }

    const bool configured_visible = readOptionalBool(node_map, "visible", true);
    widget->setProperty(kConfiguredVisibleProperty, configured_visible);
    widget->setVisible(configured_visible);
    widget->setEnabled(readOptionalBool(node_map, "enabled", true));
}

bool buildNode(const QCborMap& node_map, QWidget* parent, BuildContext* context, BuiltNode* out_node,
               std::string* error)
{
    if (!context || !out_node)
    {
        if (error)
        {
            *error = "Declarative UI build output is null";
        }
        return false;
    }

    const QString widget_type = readOptionalString(node_map, "widget");
    BuiltNode built_node;
    if (widget_type == QStringLiteral("column"))
    {
        built_node = buildColumnWidget(parent);
    }
    else if (widget_type == QStringLiteral("slider"))
    {
        built_node = buildSliderWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("spin_int"))
    {
        built_node = buildIntSpinWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("spin_double"))
    {
        built_node = buildDoubleSpinWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("checkbox"))
    {
        built_node = buildCheckboxWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("combo"))
    {
        built_node = buildComboWidget(node_map, parent, error);
    }
    else if (widget_type == QStringLiteral("line_edit"))
    {
        built_node = buildLineEditWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("file_picker") || widget_type == QStringLiteral("dir_picker"))
    {
        built_node = buildPickerWidget(node_map, parent, widget_type);
    }
    else if (widget_type == QStringLiteral("status"))
    {
        built_node = buildStatusWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("console_output"))
    {
        built_node = buildConsoleOutputWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("section_header"))
    {
        built_node = buildSectionHeaderWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("collapsible_section"))
    {
        built_node = buildCollapsibleSectionWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("info_banner"))
    {
        built_node = buildInfoBannerWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("card"))
    {
        built_node = buildCardWidget(node_map, parent);
    }
    else if (widget_type == QStringLiteral("button_group"))
    {
        built_node = buildButtonGroupWidget(node_map, parent, error);
    }
    else if (widget_type == QStringLiteral("table"))
    {
        built_node = buildTableWidget(node_map, parent, error);
    }

    if (!built_node.widget)
    {
        if (error && error->empty())
        {
            *error = "Failed to build declarative UI widget: " + widget_type.toStdString();
        }
        return false;
    }

    applyCommonNodeProperties(built_node.widget, node_map);
    if (built_node.widget_type == QStringLiteral("console_output"))
    {
        applyConsoleCollapsedVisibility(built_node.widget);
    }

    const QString widget_id = readOptionalString(node_map, "id").trimmed();
    if (!widget_id.isEmpty())
    {
        built_node.widget->setProperty(kWidgetIdProperty, widget_id);
        built_node.widget->setProperty(kWidgetTypeProperty, built_node.widget_type);
        context->widgets_by_id.insert(widget_id, built_node.widget);
        context->widget_types.insert(widget_id, built_node.widget_type);
    }

    const QCborArray children = readOptionalArray(node_map, "children");
    if (!children.isEmpty() && !built_node.child_layout)
    {
        if (error)
        {
            *error = "Widget type does not support children: " + built_node.widget_type.toStdString();
        }
        delete built_node.widget;
        return false;
    }

    std::vector<QLabel*> alignable_labels;
    for (const QCborValue& child_value : children)
    {
        if (!child_value.isMap())
        {
            if (error)
            {
                *error = "Declarative UI child nodes must be maps";
            }
            delete built_node.widget;
            return false;
        }

        BuiltNode child_node;
        if (!buildNode(child_value.toMap(), built_node.child_parent, context, &child_node, error))
        {
            delete built_node.widget;
            return false;
        }
        built_node.child_layout->addWidget(child_node.widget);
        if (isFieldTypeAlignableInGrid(child_node.widget_type))
        {
            if (auto* label = findInputFieldLabel(child_node.widget))
            {
                alignable_labels.push_back(label);
            }
        }
    }

    if (built_node.child_layout)
    {
        normalizeLayout(built_node.child_layout);
        if (built_node.widget_type == QStringLiteral("card") ||
            built_node.widget_type == QStringLiteral("collapsible_section") ||
            built_node.widget_type == QStringLiteral("column"))
        {
            if (!alignable_labels.empty())
            {
                alignFieldLabels(alignable_labels);
            }
        }
        if (built_node.widget_type == QStringLiteral("card") ||
            built_node.widget_type == QStringLiteral("collapsible_section"))
        {
            if (auto* vbox_layout = dynamic_cast<QVBoxLayout*>(built_node.child_layout))
            {
                vbox_layout->addStretch(1);
            }
        }
    }

    *out_node = built_node;
    return true;
}

bool applyCommonPatch(QWidget* widget, const QString& property_name, const QCborValue& value)
{
    if (!widget)
    {
        return false;
    }

    if (property_name == QStringLiteral("visible") && value.isBool())
    {
        widget->setProperty(kConfiguredVisibleProperty, value.toBool());
        if (widget->property(kWidgetTypeProperty).toString() == QStringLiteral("console_output"))
        {
            applyConsoleCollapsedVisibility(widget);
        }
        else
        {
            widget->setVisible(value.toBool());
        }
        return true;
    }
    if (property_name == QStringLiteral("enabled") && value.isBool())
    {
        widget->setEnabled(value.toBool());
        return true;
    }
    if (property_name == QStringLiteral("tooltip") && value.isString())
    {
        widget->setToolTip(value.toString());
        return true;
    }

    return false;
}

} // anonymous namespace

namespace an::ui::extensions
{

W_OBJECT_IMPL(ExtensionUiWidgetHost)

ExtensionUiWidgetHost::ExtensionUiWidgetHost(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("extensionUiWidgetHost"));

    root_layout_ = new QVBoxLayout(this);
    root_layout_->setContentsMargins(0, 0, 0, 0);
    root_layout_->setSpacing(tokens().spacing.sm);
    normalizeLayout(root_layout_);
}

bool ExtensionUiWidgetHost::applyDescription(const ExtensionUiDescription& description, std::string* error)
{
    clearDescription();

    const auto root_it = description.tree.find(QStringLiteral("root"));
    if (root_it == description.tree.end() || !root_it->isMap())
    {
        if (error)
        {
            *error = "Declarative UI description is missing a root widget";
        }
        return false;
    }

    BuildContext context{widgets_by_id_, widget_types_};
    BuiltNode root_node;
    if (!buildNode(root_it->toMap(), this, &context, &root_node, error))
    {
        widgets_by_id_.clear();
        widget_types_.clear();
        return false;
    }

    rendered_root_widget_ = root_node.widget;
    root_layout_->addWidget(rendered_root_widget_);
    current_description_ = description;
    current_patch_state_.reset();
    connectWidgetEventHandlers();
    refreshTheme();
    return true;
}

bool ExtensionUiWidgetHost::applyPatchState(const ExtensionUiPatchState& patch_state, std::string* error)
{
    if (!current_description_.has_value())
    {
        if (error)
        {
            *error = "ui.patch cannot be applied before ui.describe is rendered";
        }
        return false;
    }
    if (current_description_->extension_id != patch_state.extension_id ||
        current_description_->session_id != patch_state.session_id)
    {
        if (error)
        {
            *error = "ui.patch does not match the active rendered ui.describe session";
        }
        return false;
    }

    if (!applyPatchOperations(patch_state.operations, error))
    {
        return false;
    }

    current_patch_state_ = patch_state;
    return true;
}

bool ExtensionUiWidgetHost::applyLocalPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                                      std::string* error)
{
    if (!current_description_.has_value())
    {
        if (error)
        {
            *error = "Local widget patch operations cannot be applied before ui.describe is rendered";
        }
        return false;
    }

    return applyPatchOperations(operations, error);
}

bool ExtensionUiWidgetHost::applyPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                                 std::string* error)
{
    if (operations.empty())
    {
        return true;
    }

    const bool was_suppressing_events = suppress_events_;
    suppress_events_ = true;
    for (const ExtensionUiPatchOperation& operation : operations)
    {
        if (!applyPatchOperation(operation, error))
        {
            suppress_events_ = was_suppressing_events;
            return false;
        }
    }
    suppress_events_ = was_suppressing_events;

    refreshTheme();
    return true;
}

void ExtensionUiWidgetHost::connectWidgetEventHandlers()
{
    for (auto it = widgets_by_id_.cbegin(); it != widgets_by_id_.cend(); ++it)
    {
        const QString widget_id = it.key();
        QWidget* const widget = it.value();
        const QString widget_type = widget_types_.value(widget_id);
        if (!widget)
        {
            continue;
        }

        if (widget_type == QStringLiteral("slider"))
        {
            if (auto* slider = widget->findChild<QSlider*>(QString::fromLatin1(kSliderObjectName)))
            {
                QObject::connect(slider, &QSlider::valueChanged, widget, [this, widget, widget_id](const int) {
                    if (auto* spin_box =
                            widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName)))
                    {
                        emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(spin_box->value()),
                                        true);
                    }
                });
            }
            if (auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName)))
            {
                QObject::connect(spin_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), widget,
                                 [this, widget_id](const double value) {
                                     emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(value),
                                                     true);
                                 });
            }
            continue;
        }

        if (widget_type == QStringLiteral("spin_int"))
        {
            if (auto* spin_box = widget->findChild<QSpinBox*>(QString::fromLatin1(kSpinBoxObjectName)))
            {
                QObject::connect(
                    spin_box, QOverload<int>::of(&QSpinBox::valueChanged), widget, [this, widget_id](const int value) {
                        emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(value), true);
                    });
            }
            continue;
        }

        if (widget_type == QStringLiteral("spin_double"))
        {
            if (auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kDoubleSpinBoxObjectName)))
            {
                QObject::connect(spin_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), widget,
                                 [this, widget_id](const double value) {
                                     emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(value),
                                                     true);
                                 });
            }
            continue;
        }

        if (widget_type == QStringLiteral("checkbox"))
        {
            if (auto* checkbox = findCheckbox(widget))
            {
                QObject::connect(checkbox, &QCheckBox::toggled, widget, [this, widget_id](const bool checked) {
                    emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(checked), false);
                });
            }
            continue;
        }

        if (widget_type == QStringLiteral("combo"))
        {
            if (auto* combo_box = widget->findChild<QComboBox*>(QString::fromLatin1(kComboBoxObjectName)))
            {
                QObject::connect(combo_box, QOverload<int>::of(&QComboBox::currentIndexChanged), widget,
                                 [this, combo_box, widget_id](const int index) {
                                     const QVariant value =
                                         index >= 0 ? combo_box->itemData(index) : QVariant(combo_box->currentText());
                                     emitWidgetEvent(widget_id, QStringLiteral("value_changed"),
                                                     QCborValue::fromVariant(value), false);
                                 });
            }
            continue;
        }

        if (widget_type == QStringLiteral("line_edit"))
        {
            if (auto* input = dynamic_cast<an::ui::AnInputField*>(widget))
            {
                if (auto* line_edit = input->lineEdit())
                {
                    QObject::connect(
                        line_edit, &QLineEdit::textChanged, widget, [this, widget_id](const QString& text) {
                            emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(text), true);
                        });
                }
            }
            continue;
        }

        if (widget_type == QStringLiteral("file_picker") || widget_type == QStringLiteral("dir_picker"))
        {
            if (auto* line_edit = widget->findChild<QLineEdit*>(QString::fromLatin1(kPickerLineEditObjectName)))
            {
                QObject::connect(line_edit, &QLineEdit::textChanged, widget, [this, widget_id](const QString& text) {
                    emitWidgetEvent(widget_id, QStringLiteral("value_changed"), QCborValue(text), true);
                });
            }
            if (auto* button = widget->findChild<QPushButton*>(QString::fromLatin1(kPickerButtonObjectName)))
            {
                QObject::connect(button, &QPushButton::clicked, widget, [widget, widget_type]() {
                    auto* line_edit = widget->findChild<QLineEdit*>(QString::fromLatin1(kPickerLineEditObjectName));
                    if (!line_edit)
                    {
                        return;
                    }

                    auto* dialog = new QFileDialog(widget);
                    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
                    dialog->setWindowTitle(widget_type == QStringLiteral("dir_picker") ? QStringLiteral("Select Folder")
                                                                                       : QStringLiteral("Select File"));
                    dialog->setFileMode(widget_type == QStringLiteral("dir_picker") ? QFileDialog::Directory
                                                                                    : QFileDialog::ExistingFile);
                    if (widget_type == QStringLiteral("dir_picker"))
                    {
                        dialog->setOption(QFileDialog::ShowDirsOnly, true);
                    }

                    const QString current_path = line_edit->text().trimmed();
                    if (!current_path.isEmpty())
                    {
                        dialog->setDirectory(current_path);
                    }

                    QObject::connect(dialog, &QFileDialog::accepted, dialog, [dialog, line_edit]() {
                        const auto selected_paths = dialog->selectedFiles();
                        if (!selected_paths.isEmpty())
                        {
                            line_edit->setText(selected_paths.first());
                        }
                    });
                    dialog->open();
                });
            }
            continue;
        }

        if (widget_type == QStringLiteral("section_header"))
        {
            if (auto* header = dynamic_cast<an::ui::AnSectionHeader*>(widget))
            {
                if (auto* button = header->actionButton())
                {
                    QObject::connect(button, &QPushButton::clicked, widget, [this, widget_id]() {
                        emitWidgetEvent(widget_id, QStringLiteral("clicked"), QCborValue(true), false);
                    });
                }
            }
            continue;
        }

        if (widget_type == QStringLiteral("collapsible_section"))
        {
            if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
            {
                if (auto* toggle_button = section->toggleButton())
                {
                    QObject::connect(toggle_button, &QToolButton::clicked, widget,
                                     [this, section, widget_id](const bool) {
                                         emitWidgetEvent(widget_id, QStringLiteral("value_changed"),
                                                         QCborValue(section->isExpanded()), false);
                                     });
                }
            }
            continue;
        }

        if (widget_type == QStringLiteral("button_group"))
        {
            connectButtonGroupEventHandlers(widget_id, dynamic_cast<an::ui::AnButtonGroup*>(widget));
        }
    }
}

void ExtensionUiWidgetHost::connectButtonGroupEventHandlers(const QString& widget_id,
                                                            an::ui::AnButtonGroup* button_group)
{
    if (!button_group)
    {
        return;
    }

    for (QPushButton* button : button_group->buttons())
    {
        if (!button)
        {
            continue;
        }

        QObject::connect(button, &QPushButton::clicked, button_group, [this, button, widget_id]() {
            emitWidgetEvent(widget_id, QStringLiteral("value_changed"),
                            QCborValue::fromVariant(button->property("anExtensionUiValue")), false);
        });
    }
}

void ExtensionUiWidgetHost::emitWidgetEvent(const QString& widget_id, const QString& event_name,
                                            const QCborValue& value, const bool coalesced)
{
    if (suppress_events_ || widget_id.isEmpty() || event_name.isEmpty() || !current_description_.has_value())
    {
        return;
    }

    emit widgetEvent(widget_id, event_name, value, coalesced);
}

void ExtensionUiWidgetHost::clearDescription()
{
    if (rendered_root_widget_)
    {
        root_layout_->removeWidget(rendered_root_widget_);
        delete rendered_root_widget_;
        rendered_root_widget_ = nullptr;
    }

    widgets_by_id_.clear();
    widget_types_.clear();
    current_description_.reset();
    current_patch_state_.reset();
}

void ExtensionUiWidgetHost::refreshTheme()
{
    normalizeLayout(root_layout_);

    if (rendered_root_widget_)
    {
        style()->unpolish(rendered_root_widget_);
        style()->polish(rendered_root_widget_);
    }

    for (QWidget* widget : widgets_by_id_)
    {
        if (!widget)
        {
            continue;
        }

        if (auto* label = findFieldLabel(widget))
        {
            applyFieldLabelTheme(label);
        }
        if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
        {
            card->refreshTheme();
        }
        else if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
        {
            section->refreshTheme();
        }
        else if (auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(widget))
        {
            banner->refreshTheme();
        }
        else if (auto* header = dynamic_cast<an::ui::AnSectionHeader*>(widget))
        {
            header->refreshTheme();
        }
        else if (auto* input = dynamic_cast<an::ui::AnInputField*>(widget))
        {
            input->refreshTheme();
        }
        else if (auto* button_group = dynamic_cast<an::ui::AnButtonGroup*>(widget))
        {
            button_group->refreshTheme();
        }
        else if (auto* table = dynamic_cast<an::ui::AnStyledTable*>(widget))
        {
            table->refreshTheme();
        }
        else if (widget_types_.value(widget->property(kWidgetIdProperty).toString()) == QStringLiteral("status"))
        {
            if (auto* text_label = widget->findChild<QLabel*>(QString::fromLatin1(kStatusTextObjectName)))
            {
                applyStatusWidgetValue(widget, text_label->text(), widget->property(kStatusToneProperty).toString());
            }
        }
        else if (widget_types_.value(widget->property(kWidgetIdProperty).toString()) ==
                 QStringLiteral("console_output"))
        {
            applyConsoleOutputValue(widget, QCborValue::fromVariant(widget->property(kConsoleValueProperty)));
        }

        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

QWidget* ExtensionUiWidgetHost::widgetById(const QString& widget_id) const
{
    const auto it = widgets_by_id_.constFind(widget_id);
    return it != widgets_by_id_.cend() ? it.value() : nullptr;
}

QStringList ExtensionUiWidgetHost::widgetIds() const
{
    QStringList ids = widgets_by_id_.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

const std::optional<ExtensionUiDescription>& ExtensionUiWidgetHost::currentDescription() const noexcept
{
    return current_description_;
}

const std::optional<ExtensionUiPatchState>& ExtensionUiWidgetHost::currentPatchState() const noexcept
{
    return current_patch_state_;
}

bool ExtensionUiWidgetHost::snapshotValidationParams(QCborMap* out_params, std::string* error) const
{
    if (!out_params)
    {
        if (error)
        {
            *error = "Validation snapshot output is null";
        }
        return false;
    }
    if (!current_description_.has_value())
    {
        if (error)
        {
            *error = "Cannot snapshot validation params before ui.describe is rendered";
        }
        return false;
    }

    out_params->clear();
    for (const QString& widget_id : current_description_->widget_ids)
    {
        const QString widget_type = widget_types_.value(widget_id);
        if (!widgetTypeProducesValidationParam(widget_type))
        {
            continue;
        }

        QWidget* const widget = widgetById(widget_id);
        if (!widget)
        {
            if (error)
            {
                *error = "Validation snapshot references a widget that is not rendered: " + widget_id.toStdString();
            }
            return false;
        }

        QCborValue value;
        if (!readValidationSnapshotValue(widget, widget_type, &value, error))
        {
            return false;
        }
        out_params->insert(widget_id, value);
    }

    return true;
}

bool ExtensionUiWidgetHost::applyPatchOperation(const ExtensionUiPatchOperation& operation, std::string* error)
{
    QWidget* widget = widgetById(operation.widget_id);
    if (!widget)
    {
        if (error)
        {
            *error = "ui.patch references a widget that is not rendered: " + operation.widget_id.toStdString();
        }
        return false;
    }

    const QString widget_type = widget_types_.value(operation.widget_id);
    if (applyCommonPatch(widget, operation.property_name, operation.value))
    {
        return true;
    }

    if (widget_type == QStringLiteral("slider"))
    {
        if (operation.property_name == QStringLiteral("value") &&
            (operation.value.isDouble() || operation.value.isInteger()))
        {
            setSliderValue(widget, operation.value.isDouble() ? operation.value.toDouble()
                                                              : static_cast<double>(operation.value.toInteger()));
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
        if ((operation.property_name == QStringLiteral("min") || operation.property_name == QStringLiteral("max") ||
             operation.property_name == QStringLiteral("step")) &&
            (operation.value.isDouble() || operation.value.isInteger()))
        {
            const double numeric_value = operation.value.isDouble() ? operation.value.toDouble()
                                                                    : static_cast<double>(operation.value.toInteger());
            const double minimum =
                operation.property_name == QStringLiteral("min") ? numeric_value : sliderMinimum(widget);
            const double maximum =
                operation.property_name == QStringLiteral("max") ? numeric_value : sliderMaximum(widget);
            const double step = operation.property_name == QStringLiteral("step") ? numeric_value : sliderStep(widget);
            const double current_value =
                widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName))
                    ? widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kSliderSpinBoxObjectName))->value()
                    : minimum;
            configureSliderWidgets(widget, minimum, maximum, step);
            setSliderValue(widget, current_value);
            return true;
        }
    }
    else if (widget_type == QStringLiteral("spin_int"))
    {
        auto* spin_box = widget->findChild<QSpinBox*>(QString::fromLatin1(kSpinBoxObjectName));
        if (!spin_box)
        {
            if (error)
            {
                *error = "spin_int control is missing its spin box";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value") && operation.value.isInteger())
        {
            spin_box->setValue(static_cast<int>(operation.value.toInteger()));
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
    }
    else if (widget_type == QStringLiteral("spin_double"))
    {
        auto* spin_box = widget->findChild<QDoubleSpinBox*>(QString::fromLatin1(kDoubleSpinBoxObjectName));
        if (!spin_box)
        {
            if (error)
            {
                *error = "spin_double control is missing its spin box";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value") &&
            (operation.value.isDouble() || operation.value.isInteger()))
        {
            spin_box->setValue(operation.value.isDouble() ? operation.value.toDouble()
                                                          : static_cast<double>(operation.value.toInteger()));
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
    }
    else if (widget_type == QStringLiteral("checkbox"))
    {
        auto* checkbox = findCheckbox(widget);
        if (!checkbox)
        {
            if (error)
            {
                *error = "checkbox control is unavailable";
            }
            return false;
        }
        if ((operation.property_name == QStringLiteral("value") ||
             operation.property_name == QStringLiteral("checked")) &&
            operation.value.isBool())
        {
            checkbox->setChecked(operation.value.toBool());
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            if (!updateFieldLabel(widget, operation.value.toString()))
            {
                checkbox->setText(operation.value.toString());
            }
            return true;
        }
    }
    else if (widget_type == QStringLiteral("combo"))
    {
        auto* combo_box = widget->findChild<QComboBox*>(QString::fromLatin1(kComboBoxObjectName));
        if (!combo_box)
        {
            if (error)
            {
                *error = "combo control is missing its combo box";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value"))
        {
            return selectComboValue(combo_box, operation.value, error);
        }
        if (operation.property_name == QStringLiteral("options") && operation.value.isArray())
        {
            return populateComboBox(combo_box, operation.value.toArray(), {}, error);
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
    }
    else if (widget_type == QStringLiteral("line_edit"))
    {
        auto* input = dynamic_cast<an::ui::AnInputField*>(widget);
        if (!input)
        {
            if (error)
            {
                *error = "line_edit control is unavailable";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value") && operation.value.isString())
        {
            input->setText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            input->setLabelText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("placeholder") && operation.value.isString())
        {
            input->setPlaceholderText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("echo_mode") && operation.value.isString())
        {
            const QString mode = operation.value.toString().trimmed();
            input->lineEdit()->setEchoMode(mode.compare(QStringLiteral("password"), Qt::CaseInsensitive) == 0
                                               ? QLineEdit::Password
                                               : QLineEdit::Normal);
            return true;
        }
    }
    else if (widget_type == QStringLiteral("file_picker") || widget_type == QStringLiteral("dir_picker"))
    {
        auto* line_edit = widget->findChild<QLineEdit*>(QString::fromLatin1(kPickerLineEditObjectName));
        auto* button = widget->findChild<QPushButton*>(QString::fromLatin1(kPickerButtonObjectName));
        if (!line_edit || !button)
        {
            if (error)
            {
                *error = "picker control is missing its child widgets";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value") && operation.value.isString())
        {
            line_edit->setText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
        if (operation.property_name == QStringLiteral("placeholder") && operation.value.isString())
        {
            line_edit->setPlaceholderText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("button_text") && operation.value.isString())
        {
            button->setText(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("read_only") && operation.value.isBool())
        {
            line_edit->setReadOnly(operation.value.toBool());
            return true;
        }
    }
    else if (widget_type == QStringLiteral("status"))
    {
        if (operation.property_name == QStringLiteral("value") && operation.value.isString())
        {
            applyStatusWidgetValue(widget, operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("tone") && operation.value.isString())
        {
            auto* text_label = widget->findChild<QLabel*>(QString::fromLatin1(kStatusTextObjectName));
            applyStatusWidgetValue(widget, text_label ? text_label->text() : QString(), operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
    }
    else if (widget_type == QStringLiteral("console_output"))
    {
        if (operation.property_name == QStringLiteral("value"))
        {
            if (operation.value.isMap())
            {
                const QCborMap map = operation.value.toMap();
                if (map.contains(QStringLiteral("append")))
                {
                    appendConsoleOutputValue(widget, map.value(QStringLiteral("append")));
                    return true;
                }
                if (map.value(QStringLiteral("mode"))
                        .toString()
                        .compare(QStringLiteral("append"), Qt::CaseInsensitive) == 0)
                {
                    appendConsoleOutputValue(widget, map.value(QStringLiteral("entries")));
                    return true;
                }
            }
            applyConsoleOutputValue(widget, operation.value);
            return true;
        }
        if (operation.property_name == QStringLiteral("label") && operation.value.isString())
        {
            return updateFieldLabel(widget, operation.value.toString());
        }
        if (operation.property_name == QStringLiteral("min_lines") &&
            (operation.value.isInteger() || operation.value.isDouble()))
        {
            const int min_lines = operation.value.isInteger()
                                      ? static_cast<int>(operation.value.toInteger())
                                      : static_cast<int>(std::lround(operation.value.toDouble()));
            if (auto* editor = widget->findChild<QPlainTextEdit*>(QString::fromLatin1(kConsoleOutputObjectName)))
            {
                editor->setProperty(kConsoleMinLinesProperty, (std::max)(4, min_lines));
                applyConsoleOutputTheme(editor);
                return true;
            }
        }
        if (operation.property_name == QStringLiteral("collapsed_when_empty") && operation.value.isBool())
        {
            widget->setProperty(kConsoleCollapsedWhenEmptyProperty, operation.value.toBool());
            applyConsoleCollapsedVisibility(widget);
            return true;
        }
    }
    else if (widget_type == QStringLiteral("section_header"))
    {
        auto* header = dynamic_cast<an::ui::AnSectionHeader*>(widget);
        if (!header)
        {
            if (error)
            {
                *error = "section_header control is unavailable";
            }
            return false;
        }
        if ((operation.property_name == QStringLiteral("label") ||
             operation.property_name == QStringLiteral("title")) &&
            operation.value.isString())
        {
            header->setTitle(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("description") && operation.value.isString())
        {
            header->setDescription(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("action_text") && operation.value.isString())
        {
            header->setActionText(operation.value.toString());
            return true;
        }
    }
    else if (widget_type == QStringLiteral("collapsible_section"))
    {
        auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget);
        if (!section)
        {
            if (error)
            {
                *error = "collapsible_section control is unavailable";
            }
            return false;
        }
        if ((operation.property_name == QStringLiteral("label") ||
             operation.property_name == QStringLiteral("title")) &&
            operation.value.isString())
        {
            section->setTitle(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("expanded") && operation.value.isBool())
        {
            section->setExpanded(operation.value.toBool());
            return true;
        }
    }
    else if (widget_type == QStringLiteral("info_banner"))
    {
        auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(widget);
        if (!banner)
        {
            if (error)
            {
                *error = "info_banner control is unavailable";
            }
            return false;
        }
        if ((operation.property_name == QStringLiteral("message") ||
             operation.property_name == QStringLiteral("label")) &&
            operation.value.isString())
        {
            banner->setMessage(operation.value.toString());
            return true;
        }
        if (operation.property_name == QStringLiteral("tone"))
        {
            return setBannerTone(banner, operation.value, error);
        }
    }
    else if (widget_type == QStringLiteral("card"))
    {
        auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget);
        if (!card)
        {
            if (error)
            {
                *error = "card control is unavailable";
            }
            return false;
        }
        if ((operation.property_name == QStringLiteral("label") ||
             operation.property_name == QStringLiteral("title")) &&
            operation.value.isString())
        {
            card->setTitle(operation.value.toString());
            return true;
        }
    }
    else if (widget_type == QStringLiteral("button_group"))
    {
        auto* button_group = dynamic_cast<an::ui::AnButtonGroup*>(widget);
        if (!button_group)
        {
            if (error)
            {
                *error = "button_group control is unavailable";
            }
            return false;
        }
        if (operation.property_name == QStringLiteral("value"))
        {
            return selectButtonGroupValue(button_group, operation.value, error);
        }
        if (operation.property_name == QStringLiteral("options") && operation.value.isArray())
        {
            if (!populateButtonGroup(button_group, operation.value.toArray(), {}, error))
            {
                return false;
            }
            connectButtonGroupEventHandlers(operation.widget_id, button_group);
            return true;
        }
    }
    else if (widget_type == QStringLiteral("table"))
    {
        auto* table = dynamic_cast<an::ui::AnStyledTable*>(widget);
        if (!table)
        {
            if (error)
            {
                *error = "table control is unavailable";
            }
            return false;
        }

        if (operation.property_name == QStringLiteral("rows") && operation.value.isArray())
        {
            return populateTable(table, currentTableColumns(table), operation.value.toArray(), error);
        }
        if (operation.property_name == QStringLiteral("columns") && operation.value.isArray())
        {
            return populateTable(table, operation.value.toArray(), currentTableRows(table), error);
        }
    }

    if (error)
    {
        *error = "Unsupported ui.patch property for " + widget_type.toStdString() + ": " +
                 operation.property_name.toStdString();
    }
    return false;
}

} // namespace an::ui::extensions
