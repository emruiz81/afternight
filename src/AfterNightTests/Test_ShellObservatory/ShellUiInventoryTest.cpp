// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The inventory of the Observatory window's own controls, which the user guide's interface pages are held to: the
/// rail's items, the top bar's buttons, the layout icons, the floating toolbar's entries, the history strip, the
/// readout's cards and their buttons, the status line and the chrome of a task's page, each read from the running
/// window and compared with its record under `docs/user-guide/data/ui/shell`, one file a part. A part that gains,
/// loses or changes a control fails here until its record is written again, so the page that describes it is looked
/// at too - as `ProcessUiInventoryTest` holds the process windows' records beside it.
///
/// The menu bar is recorded there too (`shell.menu-bar.json`): every menu from the bar down - the menu tree's and what
/// the window adds to it, its own actions in the View, Window, File and Process menus - with its entries by their
/// texts as a menu shows them, which a page's `menu` path is held to. A menu that follows what a person did is
/// recorded as varying, and what an installed extension adds is left out.
///
/// A part is walked in the order a person meets it: its layouts in their order, and what a part places by hand -
/// the floating toolbar - from the left. A control is called by its accessible name, which is what assistive
/// technology says and what its tooltip shows; failing one, by its text, its tooltip's first line or its object
/// name. No geometry is recorded and no font, so a record is the same on every platform. What follows the image or
/// the history - the steps of the history strip, a task's item in the rail - is not a control of the part and is
/// left out.

#include "ObservatoryPages.h"

#include "actions/MenuTree.h"
#include "actions/ProcessMenuModel.h"
#include "components/AnIconRail.h"
#include "components/RailItem.h"

#include <QAbstractButton>
#include <QAction>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::observatory
{
namespace
{

/// Names a folder the inventory is written into, instead of being compared with the recorded one.
constexpr const char* kWriteVariable = "AFTERNIGHT_WRITE_SHELL_INVENTORY";

/// The recorded inventory, from the repository's root.
constexpr const char* kRecordedFolder = "docs/user-guide/data/ui/shell";

/// The id of the menu bar's record, which is kept beside the parts' and is the file's name.
constexpr const char* kMenuBarId = "shell.menu-bar";

/// One control of a part, as the inventory records it.
struct ShellControl
{
    QString label;
    QString kind;
    QString object;
    QString group;
    bool visible = true;
    QString defaultText;
    std::optional<QStringList> options;
    QStringList varies;
};

/// A part's inventory: the name a page's `controls` block calls it by, and its controls in the order a person meets
/// them.
struct ShellPart
{
    std::string id;
    QString title;
    std::vector<ShellControl> controls;
};

/// A text as a menu shows it: a mnemonic's ampersand and a shortcut after a tab dropped, white space collapsed. What it
/// ends with - the "..." of an entry that asks before it acts - is its own.
[[nodiscard]] QString menuText(const QString& text)
{
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    const QString shown = tab >= 0 ? text.left(tab) : text;
    QString plain;
    plain.reserve(shown.size());
    for (qsizetype index = 0; index < shown.size(); ++index)
    {
        if (shown[index] == QLatin1Char('&'))
        {
            if (index + 1 < shown.size() && shown[index + 1] == QLatin1Char('&'))
            {
                plain.append(QLatin1Char('&'));
                ++index;
            }
            continue;
        }
        plain.append(shown[index]);
    }
    return plain.simplified();
}

/// A text as a person reads it: rich text as its plain text, a mnemonic's ampersand and a shortcut after a tab
/// dropped, white space collapsed, a closing colon removed, and what it adds in parentheses at its end - a key, as in
/// "Close (Esc)" - left to the page.
[[nodiscard]] QString shownText(QString text)
{
    if (Qt::mightBeRichText(text))
    {
        text = QTextDocumentFragment::fromHtml(text).toPlainText();
    }
    QString plain = menuText(text);
    // What a name adds in parentheses at its end - a key, "(Esc)", or what it covers - is said by the page.
    static const QRegularExpression trailingAside(QStringLiteral("\\s*\\([^()]*\\)$"));
    plain.remove(trailingAside);
    while (plain.endsWith(QLatin1Char(':')))
    {
        plain.chop(1);
        plain = plain.trimmed();
    }
    return plain;
}

/// A tooltip's first line, as a person reads it.
[[nodiscard]] QString firstLine(const QString& text)
{
    QString plain = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    const qsizetype end = plain.indexOf(QLatin1Char('\n'));
    return end >= 0 ? plain.left(end) : plain;
}

/// Whether a widget is of a class, by its name: the components a part is made of are told apart without their headers.
[[nodiscard]] bool isClass(const QWidget* widget, const char* className)
{
    return widget != nullptr && qstrcmp(widget->metaObject()->className(), className) == 0;
}

/// A set of buttons a person takes as one choice - the display mode's Linear and Autostretch, the statistics' scales -
/// whose checked button is the choice: a segmented control, or a box of exclusive checkable buttons and nothing else.
[[nodiscard]] std::vector<QAbstractButton*> choiceButtons(const QWidget* widget)
{
    std::vector<QAbstractButton*> buttons;
    for (QObject* child : widget->children())
    {
        auto* const button = qobject_cast<QAbstractButton*>(child);
        if (button != nullptr)
        {
            if (!button->isCheckable())
            {
                return {};
            }
            buttons.push_back(button);
        }
        else if (auto* const other = qobject_cast<QWidget*>(child); other != nullptr && other->isVisibleTo(widget))
        {
            return {};
        }
    }
    const bool segmented = isClass(widget, "an::ui::AnSegmentedControl");
    const bool exclusive = std::all_of(buttons.begin(), buttons.end(), [](const QAbstractButton* button) {
        return button->autoExclusive() || button->group() != nullptr;
    });
    // A box that names the choice to assistive technology and holds nothing but its checkable buttons is one.
    const bool named = !widget->accessibleName().isEmpty() && qobject_cast<const QAbstractButton*>(widget) == nullptr;
    return buttons.size() >= 2u && (segmented || exclusive || named) ? buttons : std::vector<QAbstractButton*>{};
}

/// Whether a widget is a control a person uses, as the inventory records one.
[[nodiscard]] bool isControl(const QWidget* widget)
{
    if (qobject_cast<const QAbstractSlider*>(widget) != nullptr && qobject_cast<const QScrollArea*>(widget) == nullptr)
    {
        // A scroll bar moves what it belongs to; a slider of the shell's would be a control.
        return qstrcmp(widget->metaObject()->className(), "QScrollBar") != 0;
    }
    return qobject_cast<const QAbstractButton*>(widget) != nullptr ||
           qobject_cast<const QComboBox*>(widget) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(widget) != nullptr ||
           qobject_cast<const QLineEdit*>(widget) != nullptr || !choiceButtons(widget).empty();
}

/// The name a person calls a control by.
[[nodiscard]] QString labelOf(const QWidget* widget)
{
    // A card's header is its title; it says "folded" to assistive technology while it is.
    if (isClass(widget, "an::ui::AnReadoutCardHeader"))
    {
        if (const auto* button = qobject_cast<const QAbstractButton*>(widget))
        {
            return shownText(button->text());
        }
    }
    // An accessible name or a tooltip may say what a control does after its name - "Open Image Statistics: 13
    // statistics per channel" - and the name is what comes before.
    const auto named = [](const QString& text) {
        const qsizetype colon = text.indexOf(QStringLiteral(": "));
        return colon > 0 ? text.left(colon) : text;
    };
    QString text = shownText(named(widget->accessibleName()));
    if (text.isEmpty())
    {
        if (const auto* button = qobject_cast<const QAbstractButton*>(widget))
        {
            text = shownText(button->text());
        }
    }
    if (text.isEmpty())
    {
        text = shownText(named(firstLine(widget->toolTip())));
    }
    if (text.isEmpty())
    {
        text = widget->objectName();
    }
    return text;
}

/// What kind of control a widget is, in the words of a page's `controls` block.
[[nodiscard]] QString kindOf(const QWidget* widget)
{
    if (!choiceButtons(widget).empty())
    {
        return QStringLiteral("radio");
    }
    if (qobject_cast<const QComboBox*>(widget) != nullptr)
    {
        return QStringLiteral("combo");
    }
    if (qobject_cast<const QAbstractSpinBox*>(widget) != nullptr)
    {
        return QStringLiteral("spinbox");
    }
    if (qobject_cast<const QAbstractSlider*>(widget) != nullptr)
    {
        return QStringLiteral("slider");
    }
    if (qobject_cast<const QLineEdit*>(widget) != nullptr)
    {
        return QStringLiteral("field");
    }
    // The floating toolbar's channel is a combo box drawn as a button that shows the view's channel.
    if (widget->objectName() == QLatin1String("anFloatingToolbarChannel"))
    {
        return QStringLiteral("combo");
    }
    if (isClass(widget, "QCheckBox"))
    {
        return QStringLiteral("check");
    }
    // A button that shows an icon alone is a toolbar button; one that shows its text is a button.
    if (const auto* tool = qobject_cast<const QToolButton*>(widget))
    {
        return tool->toolButtonStyle() == Qt::ToolButtonIconOnly ? QStringLiteral("toolbutton")
                                                                 : QStringLiteral("button");
    }
    if (isClass(widget, "an::ui::AnActionButton") || isClass(widget, "an::ui::AnIconButton"))
    {
        return QStringLiteral("toolbutton");
    }
    return QStringLiteral("button");
}

/// What a control shows when the part is recorded: a check's state, a combo's or a choice's item; nothing for a
/// button.
[[nodiscard]] QString defaultOf(const QWidget* widget)
{
    if (const std::vector<QAbstractButton*> buttons = choiceButtons(widget); !buttons.empty())
    {
        for (const QAbstractButton* button : buttons)
        {
            if (button->isChecked())
            {
                return labelOf(button);
            }
        }
        return {};
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(widget))
    {
        return shownText(combo->currentText());
    }
    if (const auto* field = qobject_cast<const QLineEdit*>(widget))
    {
        return field->text();
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget))
    {
        if (widget->objectName() == QLatin1String("anFloatingToolbarChannel"))
        {
            return shownText(button->text());
        }
        if (button->isCheckable())
        {
            return button->isChecked() ? QStringLiteral("On") : QStringLiteral("Off");
        }
    }
    return {};
}

/// The choices a combo or a choice offers, when the part fixes them.
[[nodiscard]] std::optional<QStringList> optionsOf(const QWidget* widget)
{
    if (const std::vector<QAbstractButton*> buttons = choiceButtons(widget); !buttons.empty())
    {
        QStringList texts;
        for (const QAbstractButton* button : buttons)
        {
            texts.push_back(labelOf(button));
        }
        return texts;
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(widget))
    {
        QStringList texts;
        for (int index = 0; index < combo->count(); ++index)
        {
            texts.push_back(shownText(combo->itemText(index)));
        }
        return texts;
    }
    return std::nullopt;
}

/// The title of the readout card a widget sits in; empty outside a card.
[[nodiscard]] QString cardTitle(const QWidget* widget, const QWidget* part)
{
    for (const QWidget* ancestor = widget; ancestor != nullptr && ancestor != part; ancestor = ancestor->parentWidget())
    {
        for (QObject* child : ancestor->children())
        {
            const auto* const header = qobject_cast<const QWidget*>(child);
            if (header != nullptr && isClass(header, "an::ui::AnReadoutCardHeader"))
            {
                if (const auto* button = qobject_cast<const QAbstractButton*>(header))
                {
                    return shownText(button->text());
                }
            }
        }
    }
    return {};
}

/// Walks a part's widgets in the order a person meets them: its layouts in their order, what a widget places by hand
/// in the order it made it, and records its controls.
class PartWalk
{
public:
    explicit PartWalk(QWidget& part) : m_part(part) {}

    /// Leaves out a widget and everything in it: a part inside this one that has a record of its own, the history's
    /// steps.
    void skip(const QWidget* widget)
    {
        if (widget != nullptr)
        {
            m_skipped.insert(widget);
        }
    }

    /// The controls of the part, groups from the readout's cards.
    [[nodiscard]] std::vector<ShellControl> controls()
    {
        visit(&m_part);
        std::vector<ShellControl> found;
        for (QWidget* widget : m_found)
        {
            ShellControl control;
            control.label = labelOf(widget);
            control.kind = kindOf(widget);
            control.object = widget->objectName();
            control.group = cardTitle(widget, &m_part);
            control.visible = widget->isVisibleTo(&m_part);
            control.defaultText = defaultOf(widget);
            control.options = optionsOf(widget);
            found.push_back(std::move(control));
        }
        return found;
    }

private:
    void visit(QWidget* widget)
    {
        if (widget == nullptr || m_seen.contains(widget) || m_skipped.contains(widget))
        {
            return;
        }
        m_seen.insert(widget);
        if (widget != &m_part && isControl(widget))
        {
            // A slot a part fills in another state - a toolbar's entry no view has bound - has no name of its own
            // while it is hidden, and is not one of the part's controls.
            const bool unboundSlot = !widget->isVisibleTo(&m_part) && labelOf(widget) == widget->objectName();
            if (!unboundSlot)
            {
                m_found.push_back(widget);
            }
            // A card's header is a button that folds its card, and holds the card's tools.
            if (!isClass(widget, "an::ui::AnReadoutCardHeader"))
            {
                return; // a control's own parts - a combo's field, a choice's buttons - are the control
            }
        }
        if (QLayout* const layout = widget->layout(); layout != nullptr)
        {
            visitLayout(layout);
        }
        if (auto* const area = qobject_cast<QScrollArea*>(widget); area != nullptr)
        {
            visit(area->widget());
        }
        // What the layouts did not hold - a part placed by hand, as the floating toolbar places its entries - in the
        // order the part made it, which is the order it shows it.
        std::vector<QWidget*> rest;
        for (QObject* child : widget->children())
        {
            auto* const childWidget = qobject_cast<QWidget*>(child);
            if (childWidget != nullptr && !childWidget->isWindow() && !m_seen.contains(childWidget))
            {
                rest.push_back(childWidget);
            }
        }
        for (QWidget* child : rest)
        {
            visit(child);
        }
    }

    void visitLayout(QLayout* layout)
    {
        for (int index = 0; index < layout->count(); ++index)
        {
            QLayoutItem* const item = layout->itemAt(index);
            if (item == nullptr)
            {
                continue;
            }
            if (QWidget* const widget = item->widget(); widget != nullptr)
            {
                visit(widget);
            }
            else if (QLayout* const inner = item->layout(); inner != nullptr)
            {
                visitLayout(inner);
            }
        }
    }

    QWidget& m_part;
    QSet<const QWidget*> m_seen;
    QSet<const QWidget*> m_skipped;
    std::vector<QWidget*> m_found;
};

/// The rail's places and the items of its foot, as the rail lists them; a task's item follows the tasks open and is
/// not one of the rail's controls.
[[nodiscard]] std::vector<ShellControl> railControls(const an::ui::AnIconRail& rail)
{
    std::vector<ShellControl> found;
    for (const an::ui::RailItem& item : rail.items())
    {
        if (item.section == an::ui::RailSection::Task)
        {
            continue;
        }
        ShellControl control;
        control.label = shownText(item.text);
        control.kind = QStringLiteral("button");
        control.object = item.id;
        control.group = item.section == an::ui::RailSection::Foot ? QStringLiteral("Foot") : QStringLiteral("Places");
        control.visible = true;
        found.push_back(std::move(control));
    }
    return found;
}

/// A text as a JSON string.
[[nodiscard]] QString jsonString(const QString& text)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

/// One control as a JSON object on one line, its keys in the order of the process windows' records.
[[nodiscard]] QString jsonControl(const ShellControl& control)
{
    QStringList members;
    members.push_back(QStringLiteral("\"label\": ") + jsonString(control.label));
    members.push_back(QStringLiteral("\"kind\": ") + jsonString(control.kind));
    members.push_back(QStringLiteral("\"object\": ") + jsonString(control.object));
    members.push_back(QStringLiteral("\"group\": ") + jsonString(control.group));
    members.push_back(QStringLiteral("\"visible\": ") +
                      (control.visible ? QStringLiteral("true") : QStringLiteral("false")));
    if (!control.varies.contains(QStringLiteral("default")))
    {
        members.push_back(QStringLiteral("\"default\": ") + jsonString(control.defaultText));
    }
    if (control.options.has_value() && !control.varies.contains(QStringLiteral("options")))
    {
        QStringList quoted;
        for (const QString& option : *control.options)
        {
            quoted.push_back(jsonString(option));
        }
        members.push_back(QStringLiteral("\"options\": [") + quoted.join(QStringLiteral(", ")) + QLatin1Char(']'));
    }
    if (!control.varies.isEmpty())
    {
        QStringList quoted;
        for (const QString& key : control.varies)
        {
            quoted.push_back(jsonString(key));
        }
        members.push_back(QStringLiteral("\"varies\": [") + quoted.join(QStringLiteral(", ")) + QLatin1Char(']'));
    }
    return QLatin1Char('{') + members.join(QStringLiteral(", ")) + QLatin1Char('}');
}

/// A part's inventory as the text of its file, lines ending in a line feed.
[[nodiscard]] QString inventoryText(const ShellPart& part)
{
    QString text = QStringLiteral("{\n  \"id\": ") + jsonString(QString::fromStdString(part.id)) +
                   QStringLiteral(",\n  \"window\": ") + jsonString(part.title) +
                   QStringLiteral(",\n  \"controls\": [");
    for (std::size_t index = 0u; index < part.controls.size(); ++index)
    {
        text +=
            (index == 0u ? QStringLiteral("\n    ") : QStringLiteral(",\n    ")) + jsonControl(part.controls[index]);
    }
    text += part.controls.empty() ? QStringLiteral("]\n}\n") : QStringLiteral("\n  ]\n}\n");
    return text;
}

/// One menu of the menu bar: its path from the bar down, as a page's `menu` names it, and its entries - its actions
/// and its submenus - by their texts, in their order. A menu whose entries follow what a person did - the files and
/// processes used last, the favourites - is recorded as varying, with no entries.
struct MenuRecord
{
    QString path;
    QStringList entries;
    bool varies = false;
};

/// Whether a menu's entry is one an installed extension adds - its process in a category's menu, its family in the
/// Extensions menu - which follows what is installed, not the window.
[[nodiscard]] bool isExtensionEntry(const QAction& action)
{
    using an::ui::shell::ProcessMenuModel;
    return action.property(ProcessMenuModel::kExtensionProcessEntryProperty).toBool() ||
           action.property(ProcessMenuModel::kExtensionFamilyEntryProperty).toBool();
}

/// Records a menu and, after it, each of its submenus in their order.
void recordMenu(const QMenu& menu, const QString& path, std::vector<MenuRecord>& records)
{
    MenuRecord record;
    record.path = path;
    const an::ui::shell::MenuNode* const node =
        an::ui::shell::MenuTree::menuBar().menu(menu.objectName().toStdString());
    record.varies = node != nullptr && node->varies;
    std::vector<std::pair<const QMenu*, QString>> submenus;
    if (!record.varies)
    {
        for (const QAction* action : menu.actions())
        {
            if (action->isSeparator() || isExtensionEntry(*action))
            {
                continue;
            }
            const QMenu* const submenu = action->menu();
            const QString text = menuText(submenu != nullptr ? submenu->title() : action->text());
            record.entries.push_back(text);
            if (submenu != nullptr)
            {
                submenus.emplace_back(submenu, path + QStringLiteral(" > ") + text);
            }
        }
    }
    records.push_back(std::move(record));
    for (const auto& [submenu, below] : submenus)
    {
        recordMenu(*submenu, below, records);
    }
}

/// The menus of a menu bar, each menu of the bar followed by its submenus.
[[nodiscard]] std::vector<MenuRecord> menuBarRecords(const QMenuBar& bar)
{
    std::vector<MenuRecord> records;
    for (const QAction* action : bar.actions())
    {
        if (const QMenu* const menu = action->menu(); menu != nullptr && !action->isSeparator())
        {
            recordMenu(*menu, menuText(menu->title()), records);
        }
    }
    return records;
}

/// The menu bar's record as the text of its file, a menu a line, lines ending in a line feed.
[[nodiscard]] QString menuBarText(const std::vector<MenuRecord>& records)
{
    QString text = QStringLiteral("{\n  \"id\": ") + jsonString(QString::fromLatin1(kMenuBarId)) +
                   QStringLiteral(",\n  \"menus\": [");
    for (std::size_t index = 0u; index < records.size(); ++index)
    {
        const MenuRecord& record = records[index];
        QString line = QStringLiteral("{\"menu\": ") + jsonString(record.path);
        if (record.varies)
        {
            line += QStringLiteral(", \"varies\": true");
        }
        else
        {
            QStringList quoted;
            for (const QString& entry : record.entries)
            {
                quoted.push_back(jsonString(entry));
            }
            line += QStringLiteral(", \"entries\": [") + quoted.join(QStringLiteral(", ")) + QLatin1Char(']');
        }
        line += QLatin1Char('}');
        text += (index == 0u ? QStringLiteral("\n    ") : QStringLiteral(",\n    ")) + line;
    }
    text += records.empty() ? QStringLiteral("]\n}\n") : QStringLiteral("\n  ]\n}\n");
    return text;
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

/// The first line a part's text and its recorded text disagree on.
[[nodiscard]] std::string firstDifference(const QString& mine, const QString& recorded)
{
    const QStringList ours = mine.split(QLatin1Char('\n'));
    const QStringList theirs = recorded.split(QLatin1Char('\n'));
    const qsizetype shared = (std::min)(ours.size(), theirs.size());
    for (qsizetype index = 0; index < shared; ++index)
    {
        if (ours[index] != theirs[index])
        {
            return "line " + std::to_string(index + 1) + " is\n    " + ours[index].trimmed().toStdString() +
                   "\n  in the window and\n    " + theirs[index].trimmed().toStdString() + "\n  in the record";
        }
    }
    return "the window's record has " + std::to_string(ours.size()) + " lines and the recorded one " +
           std::to_string(theirs.size());
}

/// The Observatory window on the pages whose parts the inventory records.
class ShellUiInventory : public PagesCase
{
protected:
    /// The parts a workspace shows with an image open: the top bar, its layout icons, the rail, the floating toolbar,
    /// the history strip, the readout and the status line.
    static std::vector<ShellPart> workspaceParts()
    {
        std::vector<ShellPart> parts;
        an::ui::observatory::ObservatoryWindow& shown = window();

        QWidget* const topBar = shown.findChild<QWidget*>(QStringLiteral("observatoryTopBar"));
        QWidget* const layoutIcons = shown.findChild<QWidget*>(QStringLiteral("observatoryLayoutIcons"));
        QWidget* const readout = shown.findChild<QWidget*>(QStringLiteral("observatoryReadout"));
        QWidget* const statusLine = shown.findChild<QWidget*>(QStringLiteral("observatoryStatusLine"));
        EXPECT_NE(topBar, nullptr);
        EXPECT_NE(layoutIcons, nullptr) << "a workspace shows no layout icons in the top bar";
        EXPECT_NE(readout, nullptr);
        EXPECT_NE(statusLine, nullptr);
        if (topBar != nullptr)
        {
            PartWalk walk(*topBar);
            walk.skip(layoutIcons);
            parts.push_back({"shell.top-bar", QStringLiteral("Top bar"), walk.controls()});
        }
        if (layoutIcons != nullptr)
        {
            PartWalk walk(*layoutIcons);
            parts.push_back({"shell.layout-icons", QStringLiteral("Layout icons"), walk.controls()});
        }
        parts.push_back({"shell.rail", QStringLiteral("Rail"), railControls(*shown.rail())});

        QWidget* const page = shown.centre()->currentWidget();
        QWidget* const toolbar =
            page != nullptr ? page->findChild<QWidget*>(QStringLiteral("observatoryFloatingToolbar")) : nullptr;
        QWidget* const strip =
            page != nullptr ? page->findChild<QWidget*>(QStringLiteral("observatoryHistoryStrip")) : nullptr;
        EXPECT_NE(toolbar, nullptr) << "a workspace with an image shows no floating toolbar";
        EXPECT_NE(strip, nullptr) << "a workspace with an image shows no history strip";
        if (toolbar != nullptr)
        {
            PartWalk walk(*toolbar);
            parts.push_back({"shell.floating-toolbar", QStringLiteral("Floating toolbar"), walk.controls()});
        }
        if (strip != nullptr)
        {
            parts.push_back({"shell.history-strip", QStringLiteral("History strip"), historyControls(*strip)});
        }
        if (readout != nullptr)
        {
            PartWalk walk(*readout);
            parts.push_back({"shell.readout", QStringLiteral("Readout"), walk.controls()});
        }
        if (statusLine != nullptr)
        {
            PartWalk walk(*statusLine);
            std::vector<ShellControl> controls = walk.controls();
            for (ShellControl& control : controls)
            {
                // The images open are the active image's choices: they follow what a person opened.
                if (control.object == QLatin1String("observatoryViewSelector"))
                {
                    control.varies = {QStringLiteral("default"), QStringLiteral("options")};
                }
            }
            parts.push_back({"shell.status-line", QStringLiteral("Status line"), std::move(controls)});
        }
        return parts;
    }

    /// The history strip's own buttons; its steps are the image's history.
    static std::vector<ShellControl> historyControls(QWidget& strip)
    {
        PartWalk walk(strip);
        for (QAbstractButton* button : strip.findChildren<QAbstractButton*>())
        {
            if (button->objectName().isEmpty())
            {
                walk.skip(button);
            }
        }
        return walk.controls();
    }

    /// The chrome of a task's page around its process, in the order a person meets it: the task's header, the
    /// canvas's floating toolbar, the compact readout, the column's header and the page's toggles. The process's own
    /// controls are its window's record, its footer's Reset, Close and Apply every process's, and the canvas's history
    /// strip the history strip's record.
    static ShellPart taskPart(QWidget& page)
    {
        std::vector<ShellControl> controls;
        const std::vector<std::pair<const char*, QString>> pieces = {
            {"taskHeader", QStringLiteral("Task header")},
            {"observatoryTaskToolbar", QStringLiteral("Canvas")},
            {"pageReadoutSlot", QStringLiteral("Compact readout")},
            {"pageHeader", QStringLiteral("Column header")},
            {"pageToggles", QStringLiteral("Toggles")},
        };
        for (const auto& [name, group] : pieces)
        {
            QWidget* const piece = page.findChild<QWidget*>(QString::fromLatin1(name));
            EXPECT_NE(piece, nullptr) << "a task's page has no " << name;
            if (piece == nullptr)
            {
                continue;
            }
            PartWalk walk(*piece);
            for (ShellControl& control : walk.controls())
            {
                control.group = group;
                // The task's channel follows the target's channels.
                if (control.object == QLatin1String("anFloatingToolbarChannel"))
                {
                    control.varies = {QStringLiteral("default")};
                }
                controls.push_back(std::move(control));
            }
        }
        return {"shell.task-page", QStringLiteral("Task page"), std::move(controls)};
    }
};

} // namespace

// Every part of the Observatory window's own chrome holds the controls the user guide's record of it holds: their
// labels, kinds, object names, groups, visibility, what they show and their choices, in the order a person meets
// them; and the menu bar holds the menus and entries its record holds. A part or a menu that changed fails, naming it
// and the first difference, and so does a record nothing answers to. With AFTERNIGHT_WRITE_SHELL_INVENTORY naming a
// folder, the inventory is written there instead - the one case in which this writes, and never outside that folder.
TEST_F(ShellUiInventory, MatchesTheRecord)
{
    resizeTo(QSize(1536, 864));
    std::vector<ShellPart> parts;
    (void)open(Page::Workspace);
    parts = workspaceParts();
    // The workspace's image's channel and name follow the image.
    for (ShellPart& part : parts)
    {
        for (ShellControl& control : part.controls)
        {
            if (control.object == QLatin1String("anFloatingToolbarChannel"))
            {
                control.varies = {QStringLiteral("default")};
            }
        }
    }
    leave();

    QWidget* const page = open(Page::PreviewTask);
    {
        ASSERT_NE(page, nullptr) << "Curves Transformation opened no task page";
        ASSERT_NE(shownProcess(), nullptr) << "Curves Transformation opened no task page";
        parts.push_back(taskPart(*page));
    }
    leave();

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

    // The menu bar: every menu from the bar down, with its entries, which a page's `menu` path is held to.
    QMenuBar* const bar = window().menuBar();
    ASSERT_NE(bar, nullptr);
    const std::vector<MenuRecord> menus = menuBarRecords(*bar);
    ASSERT_FALSE(menus.empty()) << "the window's menu bar holds no menu";
    for (const MenuRecord& menu : menus)
    {
        EXPECT_FALSE(menu.entries.contains(QString())) << menu.path.toStdString() << ": an entry has no text";
    }

    // Each record is written into the folder named, or compared with the file recorded under its id.
    QSet<QString> expectedFiles;
    const auto hold = [&](const std::string& id, const QString& text, const char* lookAt) {
        const QString name = QString::fromStdString(id) + QStringLiteral(".json");
        expectedFiles.insert(name);
        QFile file(folder.filePath(name));
        if (!writeFolder.isEmpty())
        {
            QString withCarriageReturns = text;
            withCarriageReturns.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
            ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) << file.fileName().toStdString();
            const QByteArray bytes = withCarriageReturns.toUtf8();
            ASSERT_EQ(file.write(bytes), bytes.size()) << file.fileName().toStdString();
            return;
        }
        if (!file.open(QIODevice::ReadOnly))
        {
            ADD_FAILURE() << id << ": the inventory has no " << name.toStdString() << " under " << kRecordedFolder
                          << "; write the inventory again with " << kWriteVariable << " naming that folder";
            return;
        }
        QString recorded = QString::fromUtf8(file.readAll());
        recorded.remove(QLatin1Char('\r'));
        if (recorded != text)
        {
            ADD_FAILURE() << id << ": " << firstDifference(text, recorded)
                          << "\nwhen the window is right, write the inventory again with " << kWriteVariable
                          << " naming " << kRecordedFolder << " and look at " << lookAt;
        }
    };
    for (const ShellPart& part : parts)
    {
        // A control nothing names cannot be told from another, here or on a page.
        for (std::size_t index = 0u; index < part.controls.size(); ++index)
        {
            EXPECT_FALSE(part.controls[index].label.isEmpty())
                << part.id << ": control " << (index + 1u) << " has no name, text, tooltip or object name; give it one";
        }
        hold(part.id, inventoryText(part), "the page that describes the part");
    }
    hold(kMenuBarId, menuBarText(menus), "the pages whose `menu` names what changed");
    for (const QString& name : folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        EXPECT_TRUE(expectedFiles.contains(name)) << name.toStdString() << " under " << folder.path().toStdString()
                                                  << " is the inventory of no part; delete it";
    }
    RecordProperty("parts", static_cast<int>(parts.size()));
    RecordProperty("menus", static_cast<int>(menus.size()));
}

} // namespace an::tests::observatory
