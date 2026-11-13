// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The inventory of the controls every process window opens with, which the user guide's pages are held to: each
/// window the registry opens is walked in layout order, tabs in tab order, and what it holds - each control's label,
/// kind, object name, group, whether it shows, the text it opens with, a spin box's numbers, a combo's choices - is
/// compared with the record under `docs/user-guide/data/ui`, one file per descriptor. A window that gains, loses or
/// changes a control fails here until its record is written again, so the page that describes it is looked at too.
///
/// Every window is opened twice, on two colour views that differ in name, size and samples, under the C locale. What
/// the two openings disagree on follows the image and not the window - the size a geometry tool starts from, the open
/// images a combo lists - and is recorded as varying, without a value; so is the number of threads a window offers,
/// which follows the machine. No geometry is read, and of a font only whether a label is set as a heading, so the
/// record is the same on every platform.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessRegistry.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInputField.h"
#include "components/AnSectionHeader.h"
#include "components/AnSliderControl.h"
#include "controls/ImageViewControl.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QThreadPool>
#include <QToolButton>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::InstalledContext;
using an::tests::processes::makeDocument;
using an::tests::processes::makeTargetView;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;
using an::tests::processes::shared::closeOpenedProcessWindows;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::RegistrySnapshot;
using an::ui::process_registry::ProcessDescriptor;
using Mode = an::ui::process_registry::ProcessLaunchMode;

/// Names a folder the inventory is written into, instead of being compared with the recorded one.
constexpr const char* kWriteVariable = "AFTERNIGHT_WRITE_UI_INVENTORY";

/// The recorded inventory, from the repository's root.
constexpr const char* kRecordedFolder = "docs/user-guide/data/ui";

/// The single-image actions that act on a view's mask and open no process window.
constexpr std::array<const char*, 2u> kWindowlessActions{"actionSelect_Mask", "actionRemove_Mask"};

/// The object names `rt_preview_ui::buildHistogramToolbar` gives the three buttons every histogram toolbar of a
/// real-time preview has: a layout that holds the three is that toolbar.
constexpr std::array<const char*, 3u> kHistogramToolbarButtons{"zoomIn", "zoomOut", "zoomReset"};

/// What a control of the toolbar every real-time preview shares is marked with.
constexpr const char* kSharedPreviewToolbar = "rt-preview";

/// What a button every process window has - apply, close, reset - is marked with.
constexpr const char* kSharedWindowButton = "process-window";

/// The object names of the spin boxes that offer as many threads as the machine has processors: what they open with
/// and their largest value follow the machine, not the window, and are recorded as varying.
constexpr std::array<const char*, 2u> kThreadCountControls{"spinMaxParallelCores", "liteMaxParallelThreadsSpin"};

/// What stands in a window's title for the name of the image it was opened on.
constexpr const char* kImageNamePlaceholder = "{image}";

/// The significant digits a spin box's number is recorded with: enough for any range a person sets, and few enough
/// that two platforms' arithmetic agree on a computed one.
constexpr int kNumberDigits = 12;

/// The longest label above a control that is taken for its caption: longer, it is a sentence about the controls.
constexpr qsizetype kCaptionAboveLength = 48;

/// How far before a slider without a caption the spin box it moves may be recorded: a check box may sit between the
/// two.
constexpr std::size_t kSliderReach = 2u;

/// The titles a control sits under are joined by this into its group.
constexpr const char* kGroupSeparator = " > ";

/// What a control's label was taken from.
enum class Named
{
    ByItself,
    ByCaption,
    ByGroup,
    ByTooltip,
    ByObject
};

/// One control of a window, as the inventory records it.
struct ControlRecord
{
    QString label;
    Named named = Named::ByObject;
    QString kind;
    QString object;
    QString group;
    bool visible = true;
    QString defaultText;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::optional<int> decimals;
    QString suffix;
    std::optional<QStringList> options;
    bool slider = false;
    QString shared;
    QStringList varies;
};

/// A window's inventory: its title, the name of the image it was opened on replaced by a placeholder, and its
/// controls, in the order a person meets them.
struct WindowInventory
{
    QString title;
    std::vector<ControlRecord> controls;
};

/// A text a window shows, as a person reads it: rich text as its plain text, a mnemonic's ampersand and an action's
/// shortcut dropped, white space collapsed and a label's closing colon removed.
[[nodiscard]] QString shownText(QString text)
{
    if (Qt::mightBeRichText(text))
    {
        text = QTextDocumentFragment::fromHtml(text).toPlainText();
    }
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    QString plain;
    plain.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        if (text[index] == QLatin1Char('&'))
        {
            if (index + 1 < text.size() && text[index + 1] == QLatin1Char('&'))
            {
                plain.append(QLatin1Char('&'));
                ++index;
            }
            continue;
        }
        plain.append(text[index]);
    }
    plain = plain.simplified();
    while (plain.endsWith(QLatin1Char(':')))
    {
        plain.chop(1);
        plain = plain.trimmed();
    }
    return plain;
}

/// What a check box, a checkable button or a checkable group shows.
[[nodiscard]] QString checkedText(const Qt::CheckState state)
{
    switch (state)
    {
    case Qt::Checked:
        return QStringLiteral("On");
    case Qt::PartiallyChecked:
        return QStringLiteral("Partial");
    case Qt::Unchecked:
        break;
    }
    return QStringLiteral("Off");
}

/// A number rounded to the digits the inventory records.
[[nodiscard]] double recordedNumber(const double value)
{
    return QString::number(value, 'g', kNumberDigits).toDouble();
}

/// A number as the inventory writes it: a whole number without a fraction, any other with the recorded digits.
[[nodiscard]] QString numberText(const double value)
{
    if (std::isfinite(value) && std::nearbyint(value) == value && std::fabs(value) < 1e15)
    {
        return QString::number(static_cast<qint64>(value));
    }
    return QString::number(value, 'g', kNumberDigits);
}

/// A text as a JSON string.
[[nodiscard]] QString jsonString(const QString& text)
{
    QString quoted = QStringLiteral("\"");
    for (const QChar character : text)
    {
        switch (character.unicode())
        {
        case '"':
            quoted += QStringLiteral("\\\"");
            break;
        case '\\':
            quoted += QStringLiteral("\\\\");
            break;
        case '\n':
            quoted += QStringLiteral("\\n");
            break;
        case '\r':
            quoted += QStringLiteral("\\r");
            break;
        case '\t':
            quoted += QStringLiteral("\\t");
            break;
        default:
            if (character.unicode() < 0x20u)
            {
                quoted += QStringLiteral("\\u%1").arg(static_cast<int>(character.unicode()), 4, 16, QLatin1Char('0'));
            }
            else
            {
                quoted += character;
            }
            break;
        }
    }
    return quoted + QLatin1Char('"');
}

/// A list of texts as a JSON array.
[[nodiscard]] QString jsonList(const QStringList& texts)
{
    QStringList quoted;
    for (const QString& text : texts)
    {
        quoted.push_back(jsonString(text));
    }
    return QLatin1Char('[') + quoted.join(QStringLiteral(", ")) + QLatin1Char(']');
}

/// One control as a JSON object on one line; what varies with the image is named and not stated.
[[nodiscard]] QString jsonControl(const ControlRecord& control)
{
    QStringList members;
    const auto member = [&members, &control](const char* key, const QString& value) {
        if (!control.varies.contains(QLatin1String(key)))
        {
            members.push_back(jsonString(QLatin1String(key)) + QStringLiteral(": ") + value);
        }
    };
    member("label", jsonString(control.label));
    member("kind", jsonString(control.kind));
    member("object", jsonString(control.object));
    member("group", jsonString(control.group));
    member("visible", control.visible ? QStringLiteral("true") : QStringLiteral("false"));
    member("default", jsonString(control.defaultText));
    if (control.minimum.has_value())
    {
        member("min", numberText(*control.minimum));
    }
    if (control.maximum.has_value())
    {
        member("max", numberText(*control.maximum));
    }
    if (control.step.has_value())
    {
        member("step", numberText(*control.step));
    }
    if (control.decimals.has_value())
    {
        member("decimals", QString::number(*control.decimals));
    }
    if (!control.suffix.isEmpty())
    {
        member("suffix", jsonString(control.suffix));
    }
    if (control.options.has_value())
    {
        member("options", jsonList(*control.options));
    }
    if (control.slider)
    {
        member("slider", QStringLiteral("true"));
    }
    if (!control.shared.isEmpty())
    {
        member("shared", jsonString(control.shared));
    }
    if (!control.varies.isEmpty())
    {
        members.push_back(QStringLiteral("\"varies\": ") + jsonList(control.varies));
    }
    return QLatin1Char('{') + members.join(QStringLiteral(", ")) + QLatin1Char('}');
}

/// A window's inventory as the text of its file, lines ending in a line feed.
[[nodiscard]] QString inventoryText(const std::string& id, const WindowInventory& inventory)
{
    QString text = QStringLiteral("{\n  \"id\": ") + jsonString(QString::fromStdString(id)) +
                   QStringLiteral(",\n  \"window\": ") + jsonString(inventory.title) +
                   QStringLiteral(",\n  \"controls\": [");
    for (std::size_t index = 0u; index < inventory.controls.size(); ++index)
    {
        text += (index == 0u ? QStringLiteral("\n    ") : QStringLiteral(",\n    ")) +
                jsonControl(inventory.controls[index]);
    }
    text += inventory.controls.empty() ? QStringLiteral("]\n}\n") : QStringLiteral("\n  ]\n}\n");
    return text;
}

/// Walks one window and records its controls.
///
/// A control's label is what a person would call it by, found in this order: the text it shows itself; the caption
/// the window puts beside it - its buddy label, its form row's label, the nearest label to its left in its grid row
/// or before it in its horizontal box, or failing a label the check box there, or the short label right above it in a
/// column, looked for in each layout around it up to the container that groups it; the title of its group, when it
/// is the group's one control without a caption; the first line of its tooltip; and its object name. A button
/// without a text is an icon, which its tooltip names better than a caption that belongs to its row.
class WindowWalk
{
public:
    explicit WindowWalk(QWidget& window) : m_window(window)
    {
        for (QLabel* label : window.findChildren<QLabel*>())
        {
            if (label->buddy() != nullptr)
            {
                m_buddies.insert(label->buddy(), label);
            }
        }
    }

    /// The window's controls, in layout order.
    [[nodiscard]] std::vector<ControlRecord> controls()
    {
        m_records.clear();
        m_seen.clear();
        m_holders.clear();
        m_radioSets.clear();
        visit(&m_window, {}, {});
        nameByGroup();
        foldSliders();
        return m_records;
    }

private:
    /// Adds a title to the titles a control sits under, unless it repeats the one it would follow.
    static void addGroup(QStringList& groups, const QString& title)
    {
        if (!title.isEmpty() && (groups.isEmpty() || groups.constLast() != title))
        {
            groups.push_back(title);
        }
    }

    /// The title a container gives the controls inside it, or nothing.
    [[nodiscard]] static QString groupTitle(QWidget* container)
    {
        if (const auto* box = qobject_cast<QGroupBox*>(container))
        {
            return shownText(box->title());
        }
        if (const auto* card = dynamic_cast<an::ui::AnCardWidget*>(container))
        {
            return shownText(card->title());
        }
        if (const auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(container))
        {
            return shownText(section->title());
        }
        return {};
    }

    /// Whether a widget's own caption ends at this container: a caption outside it names the container, not what it
    /// holds.
    [[nodiscard]] bool endsCaptionSearch(QWidget* container) const
    {
        return container == nullptr || container == &m_window || qobject_cast<QGroupBox*>(container) != nullptr ||
               qobject_cast<QSplitter*>(container) != nullptr ||
               qobject_cast<QAbstractScrollArea*>(container) != nullptr ||
               qobject_cast<QStackedWidget*>(container) != nullptr || qobject_cast<QTabWidget*>(container) != nullptr ||
               dynamic_cast<an::ui::AnCardWidget*>(container) != nullptr ||
               dynamic_cast<an::ui::AnCollapsibleSection*>(container) != nullptr;
    }

    /// Whether the control shows when the window opens: nothing between it and the window is hidden, a tab's page
    /// aside - what sits on another tab is one click away, and its group names the tab.
    [[nodiscard]] bool shows(const QWidget* control) const
    {
        for (const QWidget* widget = control; widget != nullptr && widget != &m_window; widget = widget->parentWidget())
        {
            if (!widget->isHidden())
            {
                continue;
            }
            const auto* stack = qobject_cast<const QStackedWidget*>(widget->parentWidget());
            if (stack == nullptr || qobject_cast<const QTabWidget*>(stack->parentWidget()) == nullptr)
            {
                return false;
            }
        }
        return true;
    }

    /// The caption a label gives, or nothing when it shows no text.
    [[nodiscard]] static QString labelCaption(const QObject* candidate)
    {
        const auto* label = qobject_cast<const QLabel*>(candidate);
        if (label == nullptr || !label->pixmap().isNull())
        {
            return {};
        }
        return shownText(label->text());
    }

    /// The caption a check box or a radio button gives the control it switches on, or nothing.
    [[nodiscard]] static QString switchCaption(const QObject* candidate)
    {
        if (qobject_cast<const QCheckBox*>(candidate) == nullptr &&
            qobject_cast<const QRadioButton*>(candidate) == nullptr)
        {
            return {};
        }
        return shownText(qobject_cast<const QAbstractButton*>(candidate)->text());
    }

    /// Whether a label is a heading: one set in the weight the headings of a window are.
    [[nodiscard]] static bool isHeading(const QLabel* label)
    {
        return label != nullptr && label->font().weight() >= QFont::DemiBold && !labelCaption(label).isEmpty();
    }

    /// The caption a layout gives one of its items: a form's label for its field, the nearest caption to the left in
    /// a grid's row, the nearest caption before it in a horizontal box - a label's when there is one, else a check
    /// box's - or the label right above it in a column, when that is a few words and no sentence or heading.
    [[nodiscard]] static QString captionIn(QLayout* layout, QObject* item)
    {
        const auto holds = [item](QLayoutItem* candidate) {
            return candidate != nullptr && (candidate->widget() == item || candidate->layout() == item);
        };
        if (const auto* column = qobject_cast<QBoxLayout*>(layout);
            column != nullptr &&
            (column->direction() == QBoxLayout::TopToBottom || column->direction() == QBoxLayout::BottomToTop))
        {
            for (int index = 1; index < column->count(); ++index)
            {
                if (!holds(column->itemAt(index)))
                {
                    continue;
                }
                const auto* above = qobject_cast<const QLabel*>(column->itemAt(index - 1)->widget());
                const QString text = labelCaption(above);
                const bool names = above != nullptr && !isHeading(above) && !above->wordWrap() &&
                                   text.size() <= kCaptionAboveLength && !text.endsWith(QLatin1Char('.'));
                return names ? text : QString();
            }
            return {};
        }
        if (auto* form = qobject_cast<QFormLayout*>(layout))
        {
            QWidget* label = nullptr;
            if (auto* field = qobject_cast<QWidget*>(item))
            {
                label = form->labelForField(field);
            }
            else if (auto* nested = qobject_cast<QLayout*>(item))
            {
                label = form->labelForField(nested);
            }
            const QString text = labelCaption(label);
            return text.isEmpty() ? switchCaption(label) : text;
        }
        // The widgets before the item in its row, nearest first.
        std::vector<const QWidget*> before;
        if (auto* grid = qobject_cast<QGridLayout*>(layout))
        {
            int row = -1;
            int column = -1;
            for (int index = 0; index < grid->count(); ++index)
            {
                if (holds(grid->itemAt(index)))
                {
                    int rowSpan = 0;
                    int columnSpan = 0;
                    grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
                    break;
                }
            }
            std::vector<std::pair<int, const QWidget*>> inRow;
            for (int index = 0; row >= 0 && index < grid->count(); ++index)
            {
                int otherRow = 0;
                int otherColumn = 0;
                int rowSpan = 0;
                int columnSpan = 0;
                grid->getItemPosition(index, &otherRow, &otherColumn, &rowSpan, &columnSpan);
                if (otherRow == row && otherColumn < column && grid->itemAt(index)->widget() != nullptr)
                {
                    inRow.emplace_back(otherColumn, grid->itemAt(index)->widget());
                }
            }
            std::sort(inRow.begin(), inRow.end(),
                      [](const auto& left, const auto& right) { return left.first > right.first; });
            for (const auto& [otherColumn, widget] : inRow)
            {
                before.push_back(widget);
            }
        }
        else if (auto* box = qobject_cast<QBoxLayout*>(layout))
        {
            if (box->direction() != QBoxLayout::LeftToRight && box->direction() != QBoxLayout::RightToLeft)
            {
                return {};
            }
            int place = -1;
            for (int index = 0; index < box->count(); ++index)
            {
                if (holds(box->itemAt(index)))
                {
                    place = index;
                    break;
                }
            }
            for (int index = place - 1; index >= 0; --index)
            {
                if (box->itemAt(index)->widget() != nullptr)
                {
                    before.push_back(box->itemAt(index)->widget());
                }
            }
        }
        for (const QWidget* widget : before)
        {
            const QString text = labelCaption(widget);
            if (!text.isEmpty())
            {
                return text;
            }
        }
        for (const QWidget* widget : before)
        {
            const QString text = switchCaption(widget);
            if (!text.isEmpty())
            {
                return text;
            }
        }
        return {};
    }

    /// The caption the window gives a control from outside it: its buddy label, or the caption of the nearest layout
    /// around it that has one, up to the container that groups it.
    [[nodiscard]] QString outerCaption(QWidget* control) const
    {
        if (const auto buddy = m_buddies.constFind(control); buddy != m_buddies.cend())
        {
            const QString text = labelCaption(buddy.value());
            if (!text.isEmpty())
            {
                return text;
            }
        }
        QObject* item = control;
        while (item != nullptr)
        {
            QLayout* holder = m_holders.value(item, nullptr);
            if (holder == nullptr)
            {
                // Not in a layout: its parent places it, and stands for it in the parent's own layout.
                auto* placed = qobject_cast<QWidget*>(item);
                QWidget* parent = placed != nullptr ? placed->parentWidget() : nullptr;
                if (endsCaptionSearch(parent))
                {
                    return {};
                }
                item = parent;
                continue;
            }
            const QString text = captionIn(holder, item);
            if (!text.isEmpty())
            {
                return text;
            }
            if (m_holders.contains(holder))
            {
                item = holder;
                continue;
            }
            QWidget* owner = holder->parentWidget();
            if (endsCaptionSearch(owner))
            {
                return {};
            }
            item = owner;
        }
        return {};
    }

    /// The first line of a control's tooltip, as a name: without the full stop a sentence ends in.
    [[nodiscard]] static QString tooltipName(const QWidget* control)
    {
        QString tip = control->toolTip();
        if (Qt::mightBeRichText(tip))
        {
            tip = QTextDocumentFragment::fromHtml(tip).toPlainText();
        }
        QString first = shownText(tip.trimmed().section(QLatin1Char('\n'), 0, 0));
        if (first.endsWith(QLatin1Char('.')))
        {
            first.chop(1);
        }
        return first;
    }

    /// Names a record by its control: see the class.
    void label(ControlRecord& record, QWidget* control, const QString& ownText, const bool icon) const
    {
        const QString own = shownText(ownText);
        const QString tip = tooltipName(control);
        const QString caption = own.isEmpty() ? outerCaption(control) : QString();
        if (!own.isEmpty())
        {
            record.label = own;
            record.named = Named::ByItself;
        }
        else if (icon && !tip.isEmpty())
        {
            record.label = tip;
            record.named = Named::ByTooltip;
        }
        else if (!caption.isEmpty())
        {
            record.label = caption;
            record.named = Named::ByCaption;
        }
        else if (!tip.isEmpty())
        {
            record.label = tip;
            record.named = Named::ByTooltip;
        }
        else
        {
            record.label = control->objectName();
            record.named = Named::ByObject;
        }
    }

    /// A record of a control with what every kind states. `icon` says the control is a button, which a tooltip names
    /// when it shows no text.
    [[nodiscard]] ControlRecord recordOf(QWidget* control, const char* kind, const QString& ownText,
                                         const QStringList& groups, const QString& shared,
                                         const bool icon = false) const
    {
        ControlRecord record;
        label(record, control, ownText, icon);
        record.kind = QLatin1String(kind);
        record.object = control->objectName();
        record.group = groups.join(QLatin1String(kGroupSeparator));
        record.visible = shows(control);
        record.shared = shared;
        return record;
    }

    /// The actions of a menu, each a control of kind `action`, a submenu's under its title.
    void menuActions(const QMenu* menu, const QStringList& groups, const QString& shared)
    {
        for (const QAction* action : menu->actions())
        {
            if (action->isSeparator())
            {
                continue;
            }
            const QString text = shownText(action->text());
            if (const QMenu* submenu = action->menu())
            {
                QStringList inner = groups;
                addGroup(inner, text);
                menuActions(submenu, inner, shared);
                continue;
            }
            ControlRecord record;
            record.label = text.isEmpty() ? action->objectName() : text;
            record.named = text.isEmpty() ? Named::ByObject : Named::ByItself;
            record.kind = QStringLiteral("action");
            record.object = action->objectName();
            record.group = groups.join(QLatin1String(kGroupSeparator));
            record.visible = action->isVisible();
            record.defaultText =
                action->isCheckable() ? checkedText(action->isChecked() ? Qt::Checked : Qt::Unchecked) : QString();
            record.shared = shared;
            m_records.push_back(std::move(record));
        }
    }

    /// Records a control, when the widget is one; returns whether the walk is done with the widget.
    [[nodiscard]] bool recordControl(QWidget* widget, const QStringList& groups, const QString& shared)
    {
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget))
        {
            if (spin->isReadOnly())
            {
                return true;
            }
            ControlRecord record = recordOf(widget, "spinbox", {}, groups, shared);
            if (const auto* whole = qobject_cast<QSpinBox*>(spin))
            {
                record.defaultText = whole->cleanText();
                record.minimum = whole->minimum();
                record.maximum = whole->maximum();
                record.step = whole->singleStep();
                record.suffix = whole->suffix().trimmed();
            }
            else if (const auto* real = qobject_cast<QDoubleSpinBox*>(spin))
            {
                record.defaultText = real->cleanText();
                record.minimum = recordedNumber(real->minimum());
                record.maximum = recordedNumber(real->maximum());
                record.step = recordedNumber(real->singleStep());
                record.decimals = real->decimals();
                record.suffix = real->suffix().trimmed();
            }
            else
            {
                record.kind = QStringLiteral("field");
                record.defaultText = spin->text();
            }
            m_records.push_back(std::move(record));
            return true;
        }
        if (auto* slider = qobject_cast<QAbstractSlider*>(widget))
        {
            if (qobject_cast<QScrollBar*>(slider) == nullptr)
            {
                ControlRecord record = recordOf(widget, "slider", {}, groups, shared);
                record.defaultText = QString::number(slider->value());
                record.minimum = slider->minimum();
                record.maximum = slider->maximum();
                record.step = slider->singleStep();
                m_records.push_back(std::move(record));
            }
            return true;
        }
        if (auto* combo = qobject_cast<QComboBox*>(widget))
        {
            ControlRecord record = recordOf(widget, "combo", {}, groups, shared);
            record.defaultText = combo->currentText();
            QStringList options;
            for (int index = 0; index < combo->count(); ++index)
            {
                // A separator is a line between choices, not a choice.
                if (combo->itemData(index, Qt::AccessibleDescriptionRole).toString() != QLatin1String("separator"))
                {
                    options.push_back(combo->itemText(index));
                }
            }
            record.options = std::move(options);
            m_records.push_back(std::move(record));
            return true;
        }
        if (auto* radio = qobject_cast<QRadioButton*>(widget))
        {
            radioButton(radio, groups, shared);
            return true;
        }
        if (auto* check = qobject_cast<QCheckBox*>(widget))
        {
            ControlRecord record = recordOf(widget, "check", check->text(), groups, shared);
            record.defaultText = checkedText(check->checkState());
            m_records.push_back(std::move(record));
            return true;
        }
        if (auto* button = qobject_cast<QAbstractButton*>(widget))
        {
            const bool tool = qobject_cast<QToolButton*>(button) != nullptr;
            ControlRecord record =
                recordOf(widget, tool ? "toolbutton" : "button", button->text(), groups, shared, true);
            record.defaultText =
                button->isCheckable() ? checkedText(button->isChecked() ? Qt::Checked : Qt::Unchecked) : QString();
            const QString name = record.label;
            m_records.push_back(std::move(record));
            const QMenu* menu = nullptr;
            if (const auto* toolButton = qobject_cast<QToolButton*>(button))
            {
                menu = toolButton->menu();
            }
            else if (const auto* pushButton = qobject_cast<QPushButton*>(button))
            {
                menu = pushButton->menu();
            }
            if (menu != nullptr)
            {
                QStringList inner = groups;
                addGroup(inner, name);
                menuActions(menu, inner, shared);
            }
            return true;
        }
        if (auto* line = qobject_cast<QLineEdit*>(widget))
        {
            if (!line->isReadOnly())
            {
                ControlRecord record = recordOf(widget, "field", {}, groups, shared);
                record.defaultText = line->text();
                m_records.push_back(std::move(record));
            }
            return true;
        }
        if (auto* plain = qobject_cast<QPlainTextEdit*>(widget))
        {
            if (!plain->isReadOnly())
            {
                ControlRecord record = recordOf(widget, "field", {}, groups, shared);
                record.defaultText = plain->toPlainText();
                m_records.push_back(std::move(record));
            }
            return true;
        }
        if (auto* rich = qobject_cast<QTextEdit*>(widget))
        {
            if (!rich->isReadOnly())
            {
                ControlRecord record = recordOf(widget, "field", {}, groups, shared);
                record.defaultText = rich->toPlainText();
                m_records.push_back(std::move(record));
            }
            return true;
        }
        if (auto* bar = qobject_cast<QMenuBar*>(widget))
        {
            for (const QAction* action : bar->actions())
            {
                if (const QMenu* menu = action->menu())
                {
                    QStringList inner = groups;
                    addGroup(inner, shownText(action->text()));
                    menuActions(menu, inner, shared);
                }
            }
            return true;
        }
        // What shows or scrolls something and is set by no one: a caption, a progress bar, a table, a plot.
        return qobject_cast<QLabel*>(widget) != nullptr || qobject_cast<QProgressBar*>(widget) != nullptr ||
               qobject_cast<QTabBar*>(widget) != nullptr || qobject_cast<QMenu*>(widget) != nullptr ||
               (qobject_cast<QAbstractScrollArea*>(widget) != nullptr && qobject_cast<QScrollArea*>(widget) == nullptr);
    }

    /// A radio button joins the control of the buttons it excludes: one control, whose options are their texts and
    /// whose default is the one checked, named by the caption beside the first or else by its options.
    void radioButton(QRadioButton* radio, const QStringList& groups, const QString& shared)
    {
        const QObject* set = radio->group() != nullptr ? static_cast<const QObject*>(radio->group())
                                                       : static_cast<const QObject*>(radio->parentWidget());
        const QString text = shownText(radio->text());
        const auto known = m_radioSets.constFind(set);
        if (known == m_radioSets.cend())
        {
            ControlRecord record = recordOf(radio, "radio", {}, groups, shared);
            if (record.named != Named::ByCaption)
            {
                record.label = text;
                record.named = Named::ByItself;
            }
            record.options = QStringList{text};
            record.defaultText = radio->isChecked() ? text : QString();
            m_radioSets.insert(set, m_records.size());
            m_records.push_back(std::move(record));
            return;
        }
        ControlRecord& record = m_records[known.value()];
        record.options->push_back(text);
        if (radio->isChecked())
        {
            record.defaultText = text;
        }
        if (record.named != Named::ByCaption)
        {
            record.label = record.options->join(QStringLiteral(" / "));
        }
    }

    /// Whether a record is a control a person sets a value in, which a caption names.
    [[nodiscard]] static bool takesAValue(const ControlRecord& record)
    {
        return record.kind == QLatin1String("spinbox") || record.kind == QLatin1String("slider") ||
               record.kind == QLatin1String("combo") || record.kind == QLatin1String("field");
    }

    /// A control without a caption that is the one such control of its group is what the group's title names.
    void nameByGroup()
    {
        for (ControlRecord& record : m_records)
        {
            if (!takesAValue(record) || record.group.isEmpty() ||
                (record.named != Named::ByTooltip && record.named != Named::ByObject))
            {
                continue;
            }
            const auto valuesInGroup =
                std::count_if(m_records.cbegin(), m_records.cend(), [&record](const ControlRecord& other) {
                    return takesAValue(other) && other.group == record.group;
                });
            if (valuesInGroup == 1)
            {
                record.label = record.group.section(QLatin1String(kGroupSeparator), -1);
                record.named = Named::ByGroup;
            }
        }
    }

    /// A slider is the other handle of a spin box: of the one under its caption, or without a caption of its own of
    /// the nearest one recorded just before it, or else right after it. The spin box is the control, and says it has
    /// a slider.
    void foldSliders()
    {
        std::vector<std::size_t> folded;
        for (std::size_t place = 0u; place < m_records.size(); ++place)
        {
            const ControlRecord& slider = m_records[place];
            if (slider.kind != QLatin1String("slider"))
            {
                continue;
            }
            const auto pairs = [&slider](const ControlRecord& other) {
                return other.kind == QLatin1String("spinbox") && other.group == slider.group && !other.slider;
            };
            ControlRecord* partner = nullptr;
            if (slider.named == Named::ByCaption)
            {
                const auto found = std::find_if(m_records.begin(), m_records.end(), [&](const ControlRecord& other) {
                    return pairs(other) && other.label == slider.label;
                });
                partner = found != m_records.end() ? &*found : nullptr;
            }
            else
            {
                for (std::size_t back = 1u; partner == nullptr && back <= kSliderReach && back <= place; ++back)
                {
                    partner = pairs(m_records[place - back]) ? &m_records[place - back] : nullptr;
                }
                if (partner == nullptr && place + 1u < m_records.size() && pairs(m_records[place + 1u]))
                {
                    partner = &m_records[place + 1u];
                }
            }
            if (partner != nullptr)
            {
                partner->slider = true;
                folded.push_back(place);
            }
        }
        for (auto place = folded.rbegin(); place != folded.rend(); ++place)
        {
            m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(*place));
        }
    }

    /// A layout's items in the order a person reads them: a grid's and a form's by row and then by column, any
    /// other's as they were added.
    [[nodiscard]] static std::vector<QLayoutItem*> orderedItems(QLayout* layout)
    {
        std::vector<std::tuple<int, int, int>> order;
        auto* grid = qobject_cast<QGridLayout*>(layout);
        auto* form = qobject_cast<QFormLayout*>(layout);
        for (int index = 0; index < layout->count(); ++index)
        {
            int row = 0;
            int column = 0;
            if (grid != nullptr)
            {
                int rowSpan = 0;
                int columnSpan = 0;
                grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
            }
            else if (form != nullptr)
            {
                QFormLayout::ItemRole role = QFormLayout::LabelRole;
                form->getItemPosition(index, &row, &role);
                column = static_cast<int>(role);
            }
            order.emplace_back(row, column, index);
        }
        std::sort(order.begin(), order.end());
        std::vector<QLayoutItem*> items;
        for (const auto& [row, column, index] : order)
        {
            items.push_back(layout->itemAt(index));
        }
        return items;
    }

    /// Whether a layout is the histogram toolbar every real-time preview has.
    [[nodiscard]] static bool isHistogramToolbar(QLayout* layout)
    {
        std::size_t found = 0u;
        for (int index = 0; index < layout->count(); ++index)
        {
            const QWidget* item = layout->itemAt(index)->widget();
            if (qobject_cast<const QToolButton*>(item) == nullptr)
            {
                continue;
            }
            for (const char* name : kHistogramToolbarButtons)
            {
                if (item->objectName() == QLatin1String(name))
                {
                    ++found;
                }
            }
        }
        return found == kHistogramToolbarButtons.size();
    }

    void visitLayout(QLayout* layout, QStringList groups, QString shared)
    {
        if (isHistogramToolbar(layout))
        {
            shared = QLatin1String(kSharedPreviewToolbar);
        }
        const std::vector<QLayoutItem*> items = orderedItems(layout);
        for (QLayoutItem* item : items)
        {
            if (item->widget() != nullptr)
            {
                m_holders.insert(item->widget(), layout);
            }
            else if (item->layout() != nullptr)
            {
                m_holders.insert(item->layout(), layout);
            }
        }
        const auto* box = qobject_cast<QBoxLayout*>(layout);
        const bool stacksDown = box != nullptr && (box->direction() == QBoxLayout::TopToBottom ||
                                                   box->direction() == QBoxLayout::BottomToTop);
        const qsizetype depth = groups.size();
        for (QLayoutItem* item : items)
        {
            if (QWidget* inner = item->widget())
            {
                // A section header, or a heading among the rows of a column, titles what follows it in its layout,
                // until the next one.
                QString title;
                if (const auto* header = dynamic_cast<an::ui::AnSectionHeader*>(inner))
                {
                    title = shownText(header->title());
                    m_seen.insert(header->titleLabel());
                }
                else if (const auto* heading = qobject_cast<QLabel*>(inner);
                         stacksDown && isHeading(heading) && !m_seen.contains(inner))
                {
                    title = labelCaption(heading);
                }
                if (!title.isEmpty())
                {
                    groups.resize(depth);
                    addGroup(groups, title);
                }
                visit(inner, groups, shared);
            }
            else if (QLayout* nested = item->layout())
            {
                visitLayout(nested, groups, shared);
            }
        }
    }

    void visit(QWidget* widget, QStringList groups, const QString& shared)
    {
        if (widget == nullptr || m_seen.contains(widget) || (widget->isWindow() && widget != &m_window))
        {
            return;
        }
        m_seen.insert(widget);
        // What Qt makes for its own widgets - a toolbar's extension button, a scroll area's corner - is no control of
        // the window's. An image view inside the window is the image viewer, the same in every window: its toolbar is
        // not the window's, and how much of it shows depends on the width the window is drawn at.
        if (widget->objectName().startsWith(QLatin1String("qt_")) || dynamic_cast<ImageViewControl*>(widget) != nullptr)
        {
            return;
        }

        if (auto* field = dynamic_cast<an::ui::AnInputField*>(widget))
        {
            if (QLineEdit* line = field->lineEdit(); line != nullptr && !line->isReadOnly())
            {
                ControlRecord record = recordOf(line, "field", field->labelText(), groups, shared);
                record.defaultText = line->text();
                m_records.push_back(std::move(record));
            }
            return;
        }
        if (auto* sliderControl = dynamic_cast<an::ui::AnSliderControl*>(widget))
        {
            if (QSpinBox* spin = sliderControl->spinBox())
            {
                ControlRecord record = recordOf(spin, "spinbox", sliderControl->labelText(), groups, shared);
                record.defaultText = spin->cleanText();
                record.minimum = spin->minimum();
                record.maximum = spin->maximum();
                record.step = spin->singleStep();
                record.suffix = spin->suffix().trimmed();
                record.slider = true;
                m_records.push_back(std::move(record));
            }
            return;
        }
        if (auto* buttons = qobject_cast<QDialogButtonBox*>(widget))
        {
            // The box orders its buttons as the platform's dialogs do; its own list is the same everywhere.
            for (QAbstractButton* button : buttons->buttons())
            {
                m_seen.insert(button);
                const bool standard = buttons->standardButton(button) != QDialogButtonBox::NoButton;
                (void)recordControl(button, groups, standard ? QString::fromLatin1(kSharedWindowButton) : shared);
            }
            return;
        }
        if (auto* box = qobject_cast<QGroupBox*>(widget); box != nullptr && box->isCheckable())
        {
            ControlRecord record = recordOf(widget, "check", box->title(), groups, shared);
            record.defaultText = checkedText(box->isChecked() ? Qt::Checked : Qt::Unchecked);
            m_records.push_back(std::move(record));
        }
        else if (recordControl(widget, groups, shared))
        {
            return;
        }

        addGroup(groups, groupTitle(widget));
        if (auto* tabs = qobject_cast<QTabWidget*>(widget))
        {
            for (int index = 0; index < tabs->count(); ++index)
            {
                QStringList inner = groups;
                addGroup(inner, shownText(tabs->tabText(index)));
                visit(tabs->widget(index), inner, shared);
            }
            return;
        }
        if (auto* splitter = qobject_cast<QSplitter*>(widget))
        {
            for (int index = 0; index < splitter->count(); ++index)
            {
                visit(splitter->widget(index), groups, shared);
            }
            return;
        }
        if (auto* area = qobject_cast<QScrollArea*>(widget))
        {
            visit(area->widget(), groups, shared);
            return;
        }
        if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
        {
            // The card's title is the group of what it holds, not a heading inside it.
            m_seen.insert(card->headerLabel());
        }
        if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
        {
            // The button that folds the section is its title, which is the group of what it holds.
            m_seen.insert(section->toggleButton());
        }
        if (QLayout* own = widget->layout())
        {
            visitLayout(own, groups, shared);
        }
        // What no layout places, in the order it was made.
        for (QWidget* child : widget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        {
            visit(child, groups, shared);
        }
    }

    QWidget& m_window;
    QHash<const QWidget*, QLabel*> m_buddies;
    std::vector<ControlRecord> m_records;
    QSet<const QWidget*> m_seen;
    QHash<QObject*, QLayout*> m_holders;
    QHash<const QObject*, std::size_t> m_radioSets;
};

/// A colour Float32 view of a size, a name and samples of its own, made the context's only view.
[[nodiscard]] ViewHandle colourView(ProcessTestContext& context, const std::string& name, const std::uint32_t width,
                                    const std::uint32_t height, const float scale)
{
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context.engineSession(), width, height, Channels::Rgb,
        [scale](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(x + y + c) * scale; });
    EXPECT_TRUE(document.has_value());
    ViewHandle view = makeTargetView(name, std::move(*document));
    context.addViewControl(view.get());
    context.setActiveImageViewControl(view.get());
    return view;
}

/// Waits until what a window computes or loads as it opens has landed: no worker runs, and what the workers queued
/// to the GUI thread has been delivered without starting another.
void settle()
{
    for (int round = 0; round < 8; ++round)
    {
        (void)an::tests::processes::pumpUntil([]() { return QThreadPool::globalInstance()->activeThreadCount() == 0; });
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        if (QThreadPool::globalInstance()->activeThreadCount() == 0)
        {
            break;
        }
    }
}

/// Whether a descriptor opens a process window: every single-image and top-level one but the mask actions.
[[nodiscard]] bool opensAWindow(const ProcessDescriptor& descriptor)
{
    if (descriptor.launch_mode == Mode::NonInteractive)
    {
        return false;
    }
    return std::none_of(kWindowlessActions.begin(), kWindowlessActions.end(),
                        [&descriptor](const char* action) { return descriptor.action_object_name == action; });
}

/// Every process window's inventory, by descriptor id in the registry's order, each opened on a view of the given
/// name, size and samples. A window that does not open is reported and left out.
[[nodiscard]] std::vector<std::pair<std::string, WindowInventory>>
inventoriesOn(const std::string& viewName, const std::uint32_t width, const std::uint32_t height, const float scale)
{
    std::vector<std::pair<std::string, WindowInventory>> inventories;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    EXPECT_NE(context, nullptr);
    if (context == nullptr)
    {
        return inventories;
    }
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = colourView(*context, viewName, width, height, scale);

    const std::vector<ProcessDescriptor> descriptors = an::ui::process_registry::ProcessRegistry::instance().all();
    for (const ProcessDescriptor& descriptor : descriptors)
    {
        if (!opensAWindow(descriptor))
        {
            continue;
        }
        if (descriptor.launch_mode == Mode::SingleImage)
        {
            EXPECT_TRUE(static_cast<bool>(descriptor.launch_single_image)) << descriptor.id;
            descriptor.launch_single_image(view.get());
        }
        else
        {
            EXPECT_TRUE(static_cast<bool>(descriptor.launch_top_level)) << descriptor.id;
            descriptor.launch_top_level();
        }
        const std::vector<an::tests::processes::shared::OpenedProcessWindow> opened = openedProcessWindows();
        EXPECT_EQ(opened.size(), 1u) << descriptor.id;
        if (opened.size() == 1u)
        {
            // The window Help is asked from opens the page its descriptor states.
            EXPECT_FALSE(descriptor.helpPath.empty()) << descriptor.id << " states no page of the user guide";
            EXPECT_EQ(opened[0].window->helpPath().toStdString(), descriptor.helpPath) << descriptor.id;
            // F1 is the application's Help in every window. A command of the window on the same key would make it
            // ambiguous there, and an ambiguous key runs neither.
            for (const QAction* action : opened[0].window->findChildren<QAction*>())
            {
                EXPECT_NE(action->shortcut(), QKeySequence(Qt::Key_F1))
                    << descriptor.id << ": \"" << action->text().toStdString() << "\" takes F1";
            }
            for (const QShortcut* shortcut : opened[0].window->findChildren<QShortcut*>())
            {
                EXPECT_NE(shortcut->key(), QKeySequence(Qt::Key_F1)) << descriptor.id << ": a shortcut takes F1";
            }
            settle();
            WindowInventory inventory;
            // A window that sets no title is known by the name its process attaches to a view under.
            inventory.title = opened[0].window->windowTitle();
            inventory.title.replace(QString::fromStdString(viewName), QLatin1String(kImageNamePlaceholder));
            if (inventory.title.isEmpty())
            {
                inventory.title = QString::fromStdString(descriptor.process_name);
            }
            inventory.controls = WindowWalk(*opened[0].window).controls();
            inventories.emplace_back(descriptor.id, std::move(inventory));
        }
        closeOpenedProcessWindows();
    }
    return inventories;
}

/// The names of what two records of one control disagree on, as the inventory's keys.
[[nodiscard]] QStringList disagreements(const ControlRecord& first, const ControlRecord& second)
{
    QStringList keys;
    if (first.visible != second.visible)
    {
        keys.push_back(QStringLiteral("visible"));
    }
    if (first.defaultText != second.defaultText)
    {
        keys.push_back(QStringLiteral("default"));
    }
    if (first.minimum != second.minimum)
    {
        keys.push_back(QStringLiteral("min"));
    }
    if (first.maximum != second.maximum)
    {
        keys.push_back(QStringLiteral("max"));
    }
    if (first.step != second.step)
    {
        keys.push_back(QStringLiteral("step"));
    }
    if (first.decimals != second.decimals)
    {
        keys.push_back(QStringLiteral("decimals"));
    }
    if (first.options != second.options)
    {
        keys.push_back(QStringLiteral("options"));
    }
    return keys;
}

/// One inventory from two openings of a window: what they agree on, with what they do not named as varying. Fails
/// the case, naming the window, when the two openings do not hold the same controls.
[[nodiscard]] bool agreed(const std::string& id, WindowInventory& first, const WindowInventory& second)
{
    if (first.title != second.title)
    {
        ADD_FAILURE() << id << ": the window is titled \"" << first.title.toStdString() << "\" on one view and \""
                      << second.title.toStdString() << "\" on another, the views' names aside";
        return false;
    }
    if (first.controls.size() != second.controls.size())
    {
        ADD_FAILURE() << id << ": the window holds " << first.controls.size() << " controls on one view and "
                      << second.controls.size() << " on another";
        return false;
    }
    for (std::size_t index = 0u; index < first.controls.size(); ++index)
    {
        ControlRecord& control = first.controls[index];
        const ControlRecord& other = second.controls[index];
        if (control.label != other.label || control.kind != other.kind || control.object != other.object ||
            control.group != other.group)
        {
            ADD_FAILURE() << id << ": control " << (index + 1u) << " is \"" << control.label.toStdString() << "\" ("
                          << control.kind.toStdString() << ") on one view and \"" << other.label.toStdString() << "\" ("
                          << other.kind.toStdString() << ") on another";
            return false;
        }
        control.varies = disagreements(control, other);
        const bool threads =
            std::any_of(kThreadCountControls.begin(), kThreadCountControls.end(),
                        [&control](const char* name) { return control.object == QLatin1String(name); });
        for (const char* key : {"default", "max"})
        {
            if (threads && !control.varies.contains(QLatin1String(key)))
            {
                control.varies.push_back(QLatin1String(key));
            }
        }
    }
    return true;
}

/// The repository's root, found from the working directory upwards, or nothing.
[[nodiscard]] std::filesystem::path repositoryRoot()
{
    std::error_code error;
    std::filesystem::path current = std::filesystem::current_path(error);
    while (!error && !current.empty())
    {
        if (std::filesystem::exists(current / "src" / "AfterNightApp", error) &&
            std::filesystem::exists(current / "docs" / "user-guide", error))
        {
            return current;
        }
        if (current == current.root_path())
        {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

/// A JSON value as a message says it.
[[nodiscard]] std::string said(const QJsonValue& value)
{
    if (value.isUndefined())
    {
        return "not stated";
    }
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact))
        .mid(1)
        .chopped(1)
        .toStdString();
}

/// The first thing a window's inventory and the recorded one disagree on, as a sentence.
[[nodiscard]] std::string firstDifference(const QString& windowText, const QByteArray& recordedBytes)
{
    QJsonParseError error;
    const QJsonDocument recordedDocument = QJsonDocument::fromJson(recordedBytes, &error);
    if (recordedDocument.isNull() || !recordedDocument.isObject())
    {
        return "the recorded file is not a JSON object: " + error.errorString().toStdString();
    }
    const QJsonObject window = QJsonDocument::fromJson(windowText.toUtf8()).object();
    const QJsonObject recorded = recordedDocument.object();
    for (const char* key : {"id", "window"})
    {
        if (window.value(QLatin1String(key)) != recorded.value(QLatin1String(key)))
        {
            return std::string("\"") + key + "\" is " + said(window.value(QLatin1String(key))) + " in the window and " +
                   said(recorded.value(QLatin1String(key))) + " in the record";
        }
    }
    const QJsonArray windowControls = window.value(QLatin1String("controls")).toArray();
    const QJsonArray recordedControls = recorded.value(QLatin1String("controls")).toArray();
    const qsizetype shared = (std::min)(windowControls.size(), recordedControls.size());
    for (qsizetype index = 0; index < shared; ++index)
    {
        const QJsonObject mine = windowControls[index].toObject();
        const QJsonObject theirs = recordedControls[index].toObject();
        if (mine == theirs)
        {
            continue;
        }
        const std::string place =
            "control " + std::to_string(index + 1) + " (" + said(mine.value(QLatin1String("label"))) + ")";
        QStringList keys = mine.keys() + theirs.keys();
        keys.removeDuplicates();
        for (const char* first : {"label", "kind", "object", "group"})
        {
            keys.removeAll(QLatin1String(first));
            keys.push_front(QLatin1String(first));
        }
        for (const QString& key : std::as_const(keys))
        {
            if (mine.value(key) != theirs.value(key))
            {
                return place + ": \"" + key.toStdString() + "\" is " + said(mine.value(key)) + " in the window and " +
                       said(theirs.value(key)) + " in the record";
            }
        }
    }
    if (windowControls.size() != recordedControls.size())
    {
        const bool more = windowControls.size() > recordedControls.size();
        const QJsonObject extra = (more ? windowControls : recordedControls)[shared].toObject();
        return "the window holds " + std::to_string(windowControls.size()) + " controls and the record " +
               std::to_string(recordedControls.size()) + "; the first only " + (more ? "the window" : "the record") +
               " has is " + said(extra.value(QLatin1String("label")));
    }
    return "the record is not written as this test writes it";
}

/// Restores the default locale a case replaced.
class DefaultLocale
{
public:
    explicit DefaultLocale(const QLocale& locale) : m_replaced(QLocale())
    {
        QLocale::setDefault(locale);
    }
    ~DefaultLocale()
    {
        QLocale::setDefault(m_replaced);
    }
    DefaultLocale(const DefaultLocale&) = delete;
    DefaultLocale& operator=(const DefaultLocale&) = delete;

private:
    QLocale m_replaced;
};

} // namespace

// Every process window opens with the controls the user guide's inventory records for it: their labels, kinds, object
// names, groups, visibility, opening texts, numbers and choices, in layout order. A window whose controls changed
// fails, naming its descriptor and the first difference, and so does a record no window answers to. With
// AFTERNIGHT_WRITE_UI_INVENTORY naming a folder, the inventory is written there instead - the one case in which this
// writes, and never outside that folder.
TEST(ProcessUiInventoryTest, EveryProcessWindowOpensWithTheControlsTheGuideRecords)
{
    const RegistrySnapshot snapshot;
    const DefaultLocale locale(QLocale::c());

    std::vector<std::pair<std::string, WindowInventory>> inventories =
        inventoriesOn("InventoryImageOne", 8u, 6u, 1.0f / 32.0f);
    const std::vector<std::pair<std::string, WindowInventory>> again =
        inventoriesOn("InventoryImageTwo", 13u, 9u, 1.0f / 64.0f);
    ASSERT_EQ(inventories.size(), again.size());
    ASSERT_FALSE(inventories.empty());
    for (std::size_t index = 0u; index < inventories.size(); ++index)
    {
        ASSERT_EQ(inventories[index].first, again[index].first);
        ASSERT_TRUE(agreed(inventories[index].first, inventories[index].second, again[index].second));
    }

    const QString writeFolder = qEnvironmentVariable(kWriteVariable);
    QDir folder;
    if (!writeFolder.isEmpty())
    {
        ASSERT_TRUE(QDir().mkpath(writeFolder)) << writeFolder.toStdString();
        folder = QDir(writeFolder);
    }
    else
    {
        const std::filesystem::path root = repositoryRoot();
        ASSERT_FALSE(root.empty()) << "the repository's root was not found above the working directory";
        folder = QDir(QString::fromStdWString((root / kRecordedFolder).wstring()));
    }

    QSet<QString> expectedFiles;
    int written = 0;
    for (const auto& [id, inventory] : inventories)
    {
        // A control nothing names cannot be told from another, here or on a page.
        for (std::size_t index = 0u; index < inventory.controls.size(); ++index)
        {
            EXPECT_FALSE(inventory.controls[index].label.isEmpty())
                << id << ": control " << (index + 1u) << ", a " << inventory.controls[index].kind.toStdString()
                << " after \"" << (index > 0u ? inventory.controls[index - 1u].label.toStdString() : std::string())
                << "\", has no text, caption, tooltip or object name to be called by; give it one";
        }
        const QString name = QString::fromStdString(id) + QStringLiteral(".json");
        expectedFiles.insert(name);
        const QString text = inventoryText(id, inventory);
        QFile file(folder.filePath(name));
        if (!writeFolder.isEmpty())
        {
            QString withCarriageReturns = text;
            withCarriageReturns.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
            ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) << file.fileName().toStdString();
            const QByteArray bytes = withCarriageReturns.toUtf8();
            ASSERT_EQ(file.write(bytes), bytes.size()) << file.fileName().toStdString();
            ++written;
            continue;
        }
        if (!file.open(QIODevice::ReadOnly))
        {
            ADD_FAILURE() << id << ": the inventory has no " << name.toStdString() << " under " << kRecordedFolder
                          << "; write the inventory again with " << kWriteVariable << " naming that folder";
            continue;
        }
        const QByteArray recorded = file.readAll();
        QString recordedText = QString::fromUtf8(recorded);
        recordedText.remove(QLatin1Char('\r'));
        if (recordedText != text)
        {
            ADD_FAILURE() << id << ": " << firstDifference(text, recorded)
                          << "; when the window is right, write the inventory again with " << kWriteVariable
                          << " naming " << kRecordedFolder << " and look at the page that describes it";
        }
    }
    for (const QString& name : folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        EXPECT_TRUE(expectedFiles.contains(name)) << name.toStdString() << " under " << folder.path().toStdString()
                                                  << " is the inventory of no process window; delete it";
    }
    if (!writeFolder.isEmpty())
    {
        RecordProperty("writtenInventories", written);
    }
    RecordProperty("processWindows", static_cast<int>(inventories.size()));
}
