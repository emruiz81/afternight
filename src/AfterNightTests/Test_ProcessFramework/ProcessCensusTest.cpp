// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shape of every process window, which a presentation of a process other than its own window is built on: each
/// registered descriptor is launched as the application launches it, on a colour view with a grey one open beside it,
/// and what its window is made of is compared with the record beside this suite - the framework class it derives
/// from, whether it is laid out as the controls and the preview beside each other and what holds the two, whether its
/// preview checkbox puts the preview view in the preview's pane, its button box, the selectors that list the open
/// images and the channels, its menus, whether its window is of a fixed size or changes its window flags, what the
/// person marks on a view for it, and every key sequence its own actions and shortcuts carry, each with its context.
///
/// No size is read: the suite's platform measures text with the fonts of the host it runs on, so a pane's width is
/// not the same number on two hosts. Every window opens under settings of its own that hold nothing, so no case that
/// ran before decides what a window restores.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"
#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "fixtures/BaselineRecord.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageCropController.h"
#include "controls/ImagePointSelectionController.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageSampleSelectionController.h"
#include "controls/ImageStarSelectionController.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessExecution.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QBoxLayout>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QKeySequence>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::pumpQuietly;
using an::tests::processes::pumpUntil;
using an::tests::processes::framework::ContextView;
using an::tests::processes::framework::floatView;
using an::tests::processes::framework::ScopedAppContext;
using an::tests::processes::framework::suiteContext;
using an::tests::processes::framework::WindowSettingsContext;
using an::tests::processes::shared::closeOpenedProcessWindows;
using an::tests::processes::shared::OpenedProcessWindow;
using an::tests::processes::shared::openedProcessWindows;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;

/// The record, from the repository's root.
constexpr const char* kProcessShapesPath = "src/AfterNightTests/Test_ProcessFramework/data/ProcessShapes.txt";

/// The record's one section.
constexpr const char* kShapesSection = "shapes";

/// The name of the colour view every window is opened on, which a selector of the open images lists.
constexpr const char* kTargetName = "census target";

/// The name of the grey view open beside it, which a selector that takes only a grey image lists.
constexpr const char* kGreyName = "census grey";

/// The widget a process window's own layout is moved into when the window is first shown.
constexpr const char* kShellContentName = "anProcessShellContent";

/// The comment the record opens with.
[[nodiscard]] QStringList processShapesPreamble()
{
    return {
        QStringLiteral("The shape of the window every registered process opens, in the registry's order, extensions"),
        QStringLiteral("left out:"),
        QStringLiteral("  id | window class | framework class | layout | preview | button box | image selectors |"),
        QStringLiteral("  channel selector | menus | window | marks | key sequences"),
        QString(),
        QStringLiteral("framework class   the nearest of TopLevelProcessBaseControl, ImageProcessControl and"),
        QStringLiteral("                  ImageRTPreviewControl the window derives from."),
        QStringLiteral("layout            of a window with a preview: `splitter: controls, preview` when the"),
        QStringLiteral("                  splitter named mainSplitter has two panes, the first holding the window's"),
        QStringLiteral("                  inputs and the second none; `row: controls, preview` when the window has"),
        QStringLiteral("                  no such splitter and its layout is a row whose first item holds the"),
        QStringLiteral("                  inputs and whose last holds none. The controls are a widget in a splitter"),
        QStringLiteral("                  and a layout in a row."),
        QStringLiteral("preview           `checkbox, preview pane` when a checkbox named for the preview, checked,"),
        QStringLiteral("                  puts the preview view inside the splitter's second pane or the row's last"),
        QStringLiteral("                  item; `opens on` is added when the window opens with it checked."),
        QStringLiteral("button box        the standard buttons of the window's button boxes, by name; `custom` for"),
        QStringLiteral("                  each button that is not a standard one."),
        QStringLiteral("image selectors   how many combo boxes list the open images: the colour view the window"),
        QStringLiteral("                  is opened on or the grey view open beside it."),
        QStringLiteral("channel selector  `yes` when a combo box lists the channels a process runs on."),
        QStringLiteral("menus             the titles of the window's own menu bar."),
        QStringLiteral("window            `resizable` or `fixed size`, then each window hint the window adds to or"),
        QStringLiteral("                  takes from a process window's."),
        QStringLiteral("marks             what the person marks on a view for the process: an area, samples,"),
        QStringLiteral("                  stars, points. A window with a preview marks them on its preview view"),
        QStringLiteral("                  while it shows one, and the preview area every such window has is not"),
        QStringLiteral("                  listed; any other window marks them on the view it runs on."),
        QStringLiteral("key sequences     every key sequence of the window's own actions and shortcuts, with its"),
        QStringLiteral("                  context and the action's text; `the keys of Undo` for the sequences the"),
        QStringLiteral("                  platform binds to a standard key. Keys a window reads in a key press are"),
        QStringLiteral("                  not listed: Escape closes every process window."),
        QString(),
        QStringLiteral("A descriptor that opens no process window says so. No size is recorded: a pane's width"),
        QStringLiteral("follows the fonts of the host."),
        QString(),
        QStringLiteral("ProcessCensus.EveryProcessStatesItsShape in Test_ProcessFramework compares every window with"),
        QStringLiteral("this record. An entry is edited by the change that means to move it - a process given a"),
        QStringLiteral("preview, a new shortcut, a retired process; the record is not written again whole. A field"),
        QStringLiteral("is `-` when empty; `\\|` is a bar inside a text."),
    };
}

/// A text without the ampersand that marks its mnemonic and without what follows a tab.
[[nodiscard]] QString plainText(QString text)
{
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    QString plain;
    plain.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        if (text[index] == QLatin1Char('&') && index + 1 < text.size())
        {
            ++index;
        }
        plain += text[index];
    }
    return plain.trimmed();
}

/// Whether a widget is one a person sets a value with or acts through; a scroll bar is not.
[[nodiscard]] bool isInput(const QWidget& widget)
{
    if (qobject_cast<const QScrollBar*>(&widget) != nullptr)
    {
        return false;
    }
    return qobject_cast<const QAbstractButton*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSpinBox*>(&widget) != nullptr ||
           qobject_cast<const QAbstractSlider*>(&widget) != nullptr ||
           qobject_cast<const QComboBox*>(&widget) != nullptr || qobject_cast<const QLineEdit*>(&widget) != nullptr;
}

/// Whether a widget is an image view or inside one: a view brings a toolbar of its own, which is no part of the
/// window that shows the view.
[[nodiscard]] bool isPartOfAView(const QWidget& widget)
{
    for (const QWidget* at = &widget; at != nullptr; at = at->parentWidget())
    {
        if (dynamic_cast<const ImageViewControl*>(at) != nullptr)
        {
            return true;
        }
    }
    return false;
}

/// Whether a pane holds an input of the window's own, itself included.
[[nodiscard]] bool holdsInput(const QWidget& pane)
{
    if (isInput(pane) && !isPartOfAView(pane))
    {
        return true;
    }
    for (const QWidget* child : pane.findChildren<QWidget*>())
    {
        if (isInput(*child) && !isPartOfAView(*child))
        {
            return true;
        }
    }
    return false;
}

/// The nearest framework class a process window derives from.
[[nodiscard]] QString frameworkClassOf(const TopLevelProcessBaseControl& window)
{
    if (dynamic_cast<const ImageRTPreviewControl*>(&window) != nullptr)
    {
        return QStringLiteral("ImageRTPreviewControl");
    }
    if (dynamic_cast<const ImageProcessControl*>(&window) != nullptr)
    {
        return QStringLiteral("ImageProcessControl");
    }
    return QStringLiteral("TopLevelProcessBaseControl");
}

/// Whether a layout item - a widget, or a layout and what it lays out - satisfies a predicate anywhere inside it.
template <typename Predicate> [[nodiscard]] bool anywhereIn(QLayoutItem* item, const Predicate& predicate)
{
    if (item == nullptr)
    {
        return false;
    }
    if (const QWidget* widget = item->widget())
    {
        return predicate(*widget);
    }
    if (QLayout* layout = item->layout())
    {
        for (int index = 0; index < layout->count(); ++index)
        {
            if (anywhereIn(layout->itemAt(index), predicate))
            {
                return true;
            }
        }
    }
    return false;
}

/// The two sides of a window laid out as its controls and its preview beside each other: the panes of its main
/// splitter, or the first and last items of the row its layout is.
struct Sides
{
    /// `splitter`, `row`, or empty for a window that is laid out as neither.
    QString kind;

    /// How many panes the splitter has, or items the row.
    int count = 0;

    /// Whether the controls' side holds an input, and whether the preview's does.
    bool controlsHoldInputs = false;
    bool previewHoldsInputs = false;

    /// The preview's side: a pane of the splitter, or an item of the row.
    QWidget* previewPane = nullptr;
    QLayoutItem* previewItem = nullptr;
};

/// The sides of a window with a preview; of no kind for any other window. The main splitter is the one the process
/// framework looks for by its name; a window without it is read as the row its own layout is, which the window's
/// shell moves into a content widget when the window is first shown.
[[nodiscard]] Sides sidesOf(const TopLevelProcessBaseControl& window)
{
    Sides sides;
    if (dynamic_cast<const ImageRTPreviewControl*>(&window) == nullptr)
    {
        return sides;
    }
    if (const auto* splitter = window.findChild<QSplitter*>(QStringLiteral("mainSplitter")))
    {
        sides.kind = QStringLiteral("splitter");
        sides.count = splitter->count();
        if (sides.count == 2)
        {
            sides.controlsHoldInputs = holdsInput(*splitter->widget(0));
            sides.previewHoldsInputs = holdsInput(*splitter->widget(1));
            sides.previewPane = splitter->widget(1);
        }
        return sides;
    }
    const QWidget* const content = window.findChild<QWidget*>(QString::fromLatin1(kShellContentName));
    const auto* const row = qobject_cast<QHBoxLayout*>(content != nullptr ? content->layout() : window.layout());
    if (row == nullptr || row->count() < 2)
    {
        return sides;
    }
    sides.kind = QStringLiteral("row");
    sides.count = row->count();
    sides.controlsHoldInputs = anywhereIn(row->itemAt(0), holdsInput);
    sides.previewItem = row->itemAt(row->count() - 1);
    sides.previewHoldsInputs = anywhereIn(sides.previewItem, holdsInput);
    return sides;
}

/// How a window with a preview is laid out.
[[nodiscard]] QString layoutShape(const Sides& sides)
{
    if (sides.kind.isEmpty())
    {
        return {};
    }
    QString what;
    if (sides.count != 2)
    {
        what = QStringLiteral("%1 parts").arg(sides.count);
    }
    else if (sides.controlsHoldInputs && !sides.previewHoldsInputs)
    {
        what = QStringLiteral("controls, preview");
    }
    else if (sides.controlsHoldInputs)
    {
        what = QStringLiteral("inputs on both sides");
    }
    else
    {
        what = sides.previewHoldsInputs ? QStringLiteral("inputs on the second side") : QStringLiteral("no inputs");
    }
    return sides.kind + QStringLiteral(": ") + what;
}

/// The checkbox named for the preview, or null: the first of the window's own whose text says so. The preview view
/// has a checkbox for its preview area, which is the view's.
[[nodiscard]] QCheckBox* previewCheckboxOf(const QWidget& window)
{
    for (QCheckBox* checkbox : window.findChildren<QCheckBox*>())
    {
        if (!isPartOfAView(*checkbox) &&
            plainText(checkbox->text()).contains(QStringLiteral("preview"), Qt::CaseInsensitive))
        {
            return checkbox;
        }
    }
    return nullptr;
}

/// Whether a widget is another or inside it.
[[nodiscard]] bool isWithin(const QWidget* widget, const QWidget* ancestor)
{
    return widget != nullptr && ancestor != nullptr && (widget == ancestor || ancestor->isAncestorOf(widget));
}

/// What the window's preview is made of: its checkbox, checked for the reading, and where the preview view then is.
/// The checkbox is left checked; the window is closed after it is read.
[[nodiscard]] QString previewShape(TopLevelProcessBaseControl& window, const Sides& sides)
{
    QCheckBox* const checkbox = previewCheckboxOf(window);
    auto* const preview = dynamic_cast<ImageRTPreviewControl*>(&window);
    if (checkbox == nullptr)
    {
        return preview != nullptr && preview->previewView() != nullptr ? QStringLiteral("view without a checkbox")
                                                                       : QString();
    }
    QStringList parts{QStringLiteral("checkbox")};
    const bool opensChecked = checkbox->isChecked();
    if (!opensChecked && checkbox->isEnabled())
    {
        checkbox->click();
        pumpQuietly();
    }
    const ImageViewControl* const view = preview != nullptr ? preview->previewView() : nullptr;
    const auto holdsTheView = [view](const QWidget& widget) { return isWithin(view, &widget); };
    if (view == nullptr)
    {
        parts.append(QStringLiteral("no view"));
    }
    else if ((sides.previewPane != nullptr && isWithin(view, sides.previewPane)) ||
             anywhereIn(sides.previewItem, holdsTheView))
    {
        parts.append(QStringLiteral("preview pane"));
    }
    else
    {
        parts.append(QStringLiteral("view elsewhere"));
    }
    if (opensChecked)
    {
        parts.append(QStringLiteral("opens on"));
    }
    return parts.join(QStringLiteral(", "));
}

/// A standard button's name.
[[nodiscard]] QString standardButtonName(const QDialogButtonBox::StandardButton button)
{
    struct Named
    {
        QDialogButtonBox::StandardButton button;
        const char* name;
    };
    static constexpr std::array<Named, 18> kNames{{
        {QDialogButtonBox::Ok, "Ok"},
        {QDialogButtonBox::Open, "Open"},
        {QDialogButtonBox::Save, "Save"},
        {QDialogButtonBox::Cancel, "Cancel"},
        {QDialogButtonBox::Close, "Close"},
        {QDialogButtonBox::Discard, "Discard"},
        {QDialogButtonBox::Apply, "Apply"},
        {QDialogButtonBox::Reset, "Reset"},
        {QDialogButtonBox::RestoreDefaults, "RestoreDefaults"},
        {QDialogButtonBox::Help, "Help"},
        {QDialogButtonBox::SaveAll, "SaveAll"},
        {QDialogButtonBox::Yes, "Yes"},
        {QDialogButtonBox::YesToAll, "YesToAll"},
        {QDialogButtonBox::No, "No"},
        {QDialogButtonBox::NoToAll, "NoToAll"},
        {QDialogButtonBox::Abort, "Abort"},
        {QDialogButtonBox::Retry, "Retry"},
        {QDialogButtonBox::Ignore, "Ignore"},
    }};
    for (const Named& named : kNames)
    {
        if (named.button == button)
        {
            return QString::fromLatin1(named.name);
        }
    }
    return QStringLiteral("standard %1").arg(static_cast<int>(button));
}

/// The buttons of the window's button boxes: the standard ones by name, in name order - the order a box lays them
/// out in follows the platform - and `custom` for each other one.
[[nodiscard]] QString buttonBoxShape(const QWidget& window)
{
    QStringList standard;
    int custom = 0;
    for (const QDialogButtonBox* box : window.findChildren<QDialogButtonBox*>())
    {
        for (QAbstractButton* button : box->buttons())
        {
            const QDialogButtonBox::StandardButton which = box->standardButton(button);
            if (which == QDialogButtonBox::NoButton)
            {
                ++custom;
            }
            else
            {
                standard.append(standardButtonName(which));
            }
        }
    }
    standard.sort();
    for (int index = 0; index < custom; ++index)
    {
        standard.append(QStringLiteral("custom"));
    }
    return standard.join(QStringLiteral(", "));
}

/// How many of the window's combo boxes list the open images: the ones that offer, by its name, the colour view the
/// window was opened on or the grey view open beside it.
[[nodiscard]] int imageSelectorCount(const QWidget& window)
{
    int count = 0;
    for (const QComboBox* combo : window.findChildren<QComboBox*>())
    {
        if (combo->findText(QString::fromLatin1(kTargetName)) >= 0 ||
            combo->findText(QString::fromLatin1(kGreyName)) >= 0)
        {
            ++count;
        }
    }
    return count;
}

/// Whether a combo box lists the channels a process runs on: every choice of the framework's table, in its order,
/// and nothing else but the separators.
[[nodiscard]] bool listsProcessChannels(const QComboBox& combo)
{
    QStringList listed;
    for (int index = 0; index < combo.count(); ++index)
    {
        const QString text = combo.itemText(index);
        if (!text.isEmpty())
        {
            listed.append(text);
        }
    }
    QStringList choices;
    for (const an::ui::ProcessChannelChoice& choice : an::ui::processChannelChoices())
    {
        choices.append(QString::fromLatin1(choice.label));
    }
    return !listed.isEmpty() && listed == choices;
}

/// Whether the window has a selector of the channels a process runs on.
[[nodiscard]] bool hasChannelSelector(const QWidget& window)
{
    for (const QComboBox* combo : window.findChildren<QComboBox*>())
    {
        if (listsProcessChannels(*combo))
        {
            return true;
        }
    }
    return false;
}

/// The titles of the window's own menu bars' menus.
[[nodiscard]] QString menusShape(const QWidget& window)
{
    QStringList titles;
    for (const QMenuBar* bar : window.findChildren<QMenuBar*>())
    {
        for (const QAction* entry : bar->actions())
        {
            titles.append(plainText(entry->text()));
        }
    }
    return titles.join(QStringLiteral(", "));
}

/// Whether the window is of a fixed size, and each window hint it adds to or takes from the ones every process window
/// is made with.
[[nodiscard]] QString windowShape(const TopLevelProcessBaseControl& window)
{
    struct Hint
    {
        Qt::WindowType flag;
        const char* name;
    };
    static constexpr std::array<Hint, 9> kHints{{
        {Qt::WindowTitleHint, "title"},
        {Qt::WindowSystemMenuHint, "system menu"},
        {Qt::WindowCloseButtonHint, "close button"},
        {Qt::WindowMinimizeButtonHint, "minimize button"},
        {Qt::WindowMaximizeButtonHint, "maximize button"},
        {Qt::WindowStaysOnTopHint, "stays on top"},
        {Qt::FramelessWindowHint, "frameless"},
        {Qt::CustomizeWindowHint, "customized"},
        {Qt::WindowContextHelpButtonHint, "help button"},
    }};
    constexpr Qt::WindowFlags kProcessWindowHints = Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                    Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint |
                                                    Qt::WindowMaximizeButtonHint;

    const bool fixed = window.property("anFixedSizeWindow").toBool() ||
                       (window.minimumSize() == window.maximumSize() && !window.minimumSize().isEmpty());
    QStringList parts{fixed ? QStringLiteral("fixed size") : QStringLiteral("resizable")};
    if (window.windowType() != Qt::Window)
    {
        parts.append(QStringLiteral("window type %1").arg(static_cast<int>(window.windowType())));
    }
    const Qt::WindowFlags flags = window.windowFlags();
    for (const Hint& hint : kHints)
    {
        const bool has = flags.testFlag(hint.flag);
        const bool made = kProcessWindowHints.testFlag(hint.flag);
        if (has != made)
        {
            parts.append(QStringLiteral("%1 %2").arg(has ? QStringLiteral("with") : QStringLiteral("without"),
                                                     QString::fromLatin1(hint.name)));
        }
    }
    return parts.join(QStringLiteral(", "));
}

/// What the person marks on a view for the process. Every window with a preview is an area selector for its preview
/// area, so an area is listed only for a window without one, which marks it on the view it runs on.
[[nodiscard]] QString marksShape(TopLevelProcessBaseControl& window)
{
    QStringList marks;
    if (dynamic_cast<ImageRTPreviewControl*>(&window) == nullptr &&
        dynamic_cast<ImageAreaSelectorControl*>(&window) != nullptr)
    {
        marks.append(QStringLiteral("an area"));
    }
    if (dynamic_cast<ImageSampleSelectorControl*>(&window) != nullptr)
    {
        marks.append(QStringLiteral("samples"));
    }
    if (dynamic_cast<ImageStarSelectorControl*>(&window) != nullptr)
    {
        marks.append(QStringLiteral("stars"));
    }
    if (dynamic_cast<ImagePointSelectorControl*>(&window) != nullptr)
    {
        marks.append(QStringLiteral("points"));
    }
    return marks.join(QStringLiteral(", "));
}

/// A shortcut context's name.
[[nodiscard]] QString contextName(const Qt::ShortcutContext context)
{
    switch (context)
    {
    case Qt::WidgetShortcut:
        return QStringLiteral("widget");
    case Qt::WidgetWithChildrenShortcut:
        return QStringLiteral("widget with children");
    case Qt::WindowShortcut:
        return QStringLiteral("window");
    case Qt::ApplicationShortcut:
        return QStringLiteral("application");
    }
    return QStringLiteral("context %1").arg(static_cast<int>(context));
}

/// The standard key a list of key sequences is the bindings of, by its name, or empty. Only a list of more than one
/// sequence is looked up: the bindings of a standard key are the platform's, so they are recorded by the key's name,
/// and a single sequence is recorded as itself.
[[nodiscard]] QString standardKeyNameOf(const QList<QKeySequence>& keys)
{
    struct Named
    {
        QKeySequence::StandardKey key;
        const char* name;
    };
    static constexpr std::array<Named, 12> kStandardKeys{{
        {QKeySequence::Undo, "Undo"},
        {QKeySequence::Redo, "Redo"},
        {QKeySequence::Find, "Find"},
        {QKeySequence::Delete, "Delete"},
        {QKeySequence::Copy, "Copy"},
        {QKeySequence::Cut, "Cut"},
        {QKeySequence::Paste, "Paste"},
        {QKeySequence::SelectAll, "SelectAll"},
        {QKeySequence::Open, "Open"},
        {QKeySequence::Save, "Save"},
        {QKeySequence::Refresh, "Refresh"},
        {QKeySequence::Close, "Close"},
    }};
    if (keys.size() < 2)
    {
        return {};
    }
    for (const Named& named : kStandardKeys)
    {
        if (QKeySequence::keyBindings(named.key) == keys)
        {
            return QString::fromLatin1(named.name);
        }
    }
    return {};
}

/// Every key sequence the window's own actions and shortcuts carry, each with its context and, for an action, its
/// text; in the order the window holds them, each once. What belongs to an image view the window shows is the
/// view's.
[[nodiscard]] QString keySequencesShape(const QWidget& window)
{
    QStringList sequences;
    const auto add = [&sequences](const QString& keys, const Qt::ShortcutContext context, const QString& what) {
        QString entry = QStringLiteral("%1 (%2").arg(keys, contextName(context));
        if (!what.isEmpty())
        {
            entry += QStringLiteral(": ") + what;
        }
        entry += QLatin1Char(')');
        if (!sequences.contains(entry))
        {
            sequences.append(entry);
        }
    };
    const auto addAll = [&add](const QList<QKeySequence>& keys, const Qt::ShortcutContext context,
                               const QString& what) {
        const QString standard = standardKeyNameOf(keys);
        if (!standard.isEmpty())
        {
            add(QStringLiteral("the keys of ") + standard, context, what);
            return;
        }
        for (const QKeySequence& sequence : keys)
        {
            if (!sequence.isEmpty())
            {
                add(sequence.toString(QKeySequence::PortableText), context, what);
            }
        }
    };
    const auto ofAView = [](const QObject* object) {
        for (; object != nullptr; object = object->parent())
        {
            if (dynamic_cast<const ImageViewControl*>(object) != nullptr)
            {
                return true;
            }
        }
        return false;
    };
    for (const QAction* action : window.findChildren<QAction*>())
    {
        if (!ofAView(action))
        {
            addAll(action->shortcuts(), action->shortcutContext(), plainText(action->text()));
        }
    }
    for (const QShortcut* shortcut : window.findChildren<QShortcut*>())
    {
        if (!ofAView(shortcut))
        {
            addAll(shortcut->keys(), shortcut->context(), {});
        }
    }
    return sequences.join(QStringLiteral("; "));
}

/// The record's entry of a process window.
[[nodiscard]] QString shapeEntry(const ProcessDescriptor& descriptor, const OpenedProcessWindow& opened)
{
    TopLevelProcessBaseControl& window = *opened.window;
    // What the window is made with is read before its preview is asked for: checking the preview resizes the window.
    const QString windowHints = windowShape(window);
    const Sides sides = sidesOf(window);
    const QString layout = layoutShape(sides);
    const QString buttons = buttonBoxShape(window);
    const int imageSelectors = imageSelectorCount(window);
    const bool channelSelector = hasChannelSelector(window);
    const QString menus = menusShape(window);
    const QString marks = marksShape(window);
    const QString keys = keySequencesShape(window);
    const QString preview = previewShape(window, sides);
    return an::tests::fixtures::baselineEntry({
        QString::fromStdString(descriptor.id),
        QString::fromStdString(opened.className),
        frameworkClassOf(window),
        layout,
        preview,
        buttons,
        imageSelectors == 0 ? QString() : QString::number(imageSelectors),
        channelSelector ? QStringLiteral("yes") : QString(),
        menus,
        windowHints,
        marks,
        keys,
    });
}

/// The top-level widgets shown now that are no process window.
[[nodiscard]] std::vector<QPointer<QWidget>> otherShownWindows()
{
    std::vector<QPointer<QWidget>> shown;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (widget->isVisible() && dynamic_cast<TopLevelProcessBaseControl*>(widget) == nullptr)
        {
            shown.emplace_back(widget);
        }
    }
    return shown;
}

/// Closes the windows a launch showed that are no process window - the mask selector an action of the image menu
/// opens - and deletes the ones that do not delete themselves.
void closeWindowsShownSince(const std::vector<QPointer<QWidget>>& before)
{
    for (const QPointer<QWidget>& shown : otherShownWindows())
    {
        bool wasShown = false;
        for (const QPointer<QWidget>& earlier : before)
        {
            wasShown = wasShown || earlier == shown;
        }
        if (wasShown || shown.isNull())
        {
            continue;
        }
        const bool deletesItself = shown->testAttribute(Qt::WA_DeleteOnClose);
        shown->close();
        if (!deletesItself && !shown.isNull())
        {
            shown->deleteLater();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

// Every process of the application opens the window the record describes: a process laid out as the controls and the
// preview beside each other stays so, one with a preview checkbox keeps it and the pane its preview lands in, a
// window of a fixed size stays one, and a key sequence a process defines is the record's. A process that changes
// shape fails here until its entry says so: whatever shows a process other than as its own window is arranged from
// these parts.
TEST(ProcessCensus, EveryProcessStatesItsShape)
{
    WindowSettingsContext settings(suiteContext());
    const ScopedAppContext installed(settings);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    const std::unique_ptr<ContextView> target =
        floatView(kTargetName, 96u, 64u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<float>(x + y + channel) / 256.0f;
        });
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(static_cast<bool>(*target));
    const std::unique_ptr<ContextView> grey =
        floatView(kGreyName, 96u, 64u, Channels::Grey,
                  [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + y) / 256.0f; });
    ASSERT_NE(grey, nullptr);
    ASSERT_TRUE(static_cast<bool>(*grey));

    QStringList entries;
    // The registry is copied: a launch reads it, and nothing here may read a descriptor a case's change moved.
    const std::vector<ProcessDescriptor> descriptors = an::ui::process_registry::ProcessRegistry::instance().all();
    for (const ProcessDescriptor& descriptor : descriptors)
    {
        if (descriptor.is_extension_process)
        {
            continue;
        }
        const QString id = QString::fromStdString(descriptor.id);
        if (descriptor.launch_mode == ProcessLaunchMode::NonInteractive)
        {
            entries.append(an::tests::fixtures::baselineEntry({id, QStringLiteral("runs without a window")}));
            continue;
        }

        suiteContext().setActiveImageViewControl(target->get());
        const std::vector<QPointer<QWidget>> shownBefore = otherShownWindows();
        if (descriptor.launch_mode == ProcessLaunchMode::SingleImage)
        {
            ASSERT_TRUE(static_cast<bool>(descriptor.launch_single_image)) << descriptor.id;
            descriptor.launch_single_image(target->get());
        }
        else
        {
            ASSERT_TRUE(static_cast<bool>(descriptor.launch_top_level)) << descriptor.id;
            descriptor.launch_top_level();
        }
        pumpQuietly();

        const std::vector<OpenedProcessWindow> opened = openedProcessWindows();
        ASSERT_LE(opened.size(), 1u) << descriptor.id;
        if (opened.empty())
        {
            entries.append(an::tests::fixtures::baselineEntry({id, QStringLiteral("opens no process window")}));
        }
        else
        {
            entries.append(shapeEntry(descriptor, opened.front()));
        }
        closeOpenedProcessWindows();
        closeWindowsShownSince(shownBefore);
        ASSERT_TRUE(pumpUntil([]() { return QThreadPool::globalInstance()->activeThreadCount() == 0; }))
            << descriptor.id;
    }

    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(kProcessShapesPath), processShapesPreamble());
    EXPECT_TRUE(record.matches(QString::fromLatin1(kShapesSection), entries));
}
