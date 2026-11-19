// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The controls the native process window builds from an extension's parameters, held to a record.
///
/// `data/NativeWindowSchemas.json` holds the parameters of every bundled extension and author template, as each one's
/// `get_params()` answered when the record was made, and two schemas written for this case that use every kind the
/// window draws, each field of it and the lenient readings of a malformed entry. The case builds a window from each and
/// writes down every widget its parameter panel holds, under the widget that holds it, with its class, its name and
/// marks, its state, its text and values, its sizes and where its layout puts it; and the widgets the Tab key reaches,
/// in its order, which is the order they were made in. `data/NativeWindowInventory.txt` is what that was when the
/// record was made: a change to how the window builds its controls fails the case until the entry it moves is edited.

// The window's header dereferences a guarded view, whose class must be complete before it.
#include "controls/ImageViewControl.h"

#include "controls/ExtensionNativeProcessWindow.h"
#include "fixtures/BaselineRecord.h"

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{

using an::tests::fixtures::baselineEntry;
using an::tests::fixtures::BaselineRecord;

/// The record and the schemas, from the repository's root.
constexpr const char* kInventoryPath = "src/AfterNightTests/Test_ExtensionHostNative/data/NativeWindowInventory.txt";
constexpr const char* kSchemasPath = "src/AfterNightTests/Test_ExtensionHostNative/data/NativeWindowSchemas.json";

[[nodiscard]] QStringList inventoryPreamble()
{
    return {
        QStringLiteral("The widgets of the native process window's parameter panel, built from each schema of"),
        QStringLiteral("NativeWindowSchemas.json, a section a schema. A widget a line, in the order it was made,"),
        QStringLiteral("indented four spaces under the widget that holds it:"),
        QStringLiteral("  class | name | marks | state | tooltip | content | sizes | own layout | place"),
        QString(),
        QStringLiteral("marks       the widget's own properties that start with `an`, as name=value."),
        QStringLiteral("state       `hidden` when it is hidden itself, `disabled` when it is not enabled."),
        QStringLiteral("content     what the widget shows or holds: a label's text, a button's text and check,"),
        QStringLiteral("            an edit's text and placeholder, a spin box's range, step and value, a slider's,"),
        QStringLiteral("            a combo box's entries as text=value, a tab widget's tabs, a banner's message"),
        QStringLiteral("            and tone, a card's or a section's title."),
        QStringLiteral("sizes       the minimum and maximum the widget was given, and its size policy."),
        QStringLiteral("own layout  the class of the widget's layout, its margins and its spacing."),
        QStringLiteral("place       where the layout of the widget's parent puts it: the path of item indexes"),
        QStringLiteral("            through nested layouts, a grid's row and column, and the alignment."),
        QString(),
        QStringLiteral("Widgets Qt makes inside its own (named qt_...) are not listed, nor walked through but for"),
        QStringLiteral("a scroll area's viewport and a tab widget's pages. A collapsible section's content is sized"),
        QStringLiteral("to its fonts, so its size is not recorded."),
        QString(),
        QStringLiteral(
            "A section named for a schema and `focus` lists the widgets the Tab key reaches in the panel, in its"),
        QStringLiteral(
            "order, as `class | name | id` - the id of the widget or of the row it sits in. That order is the"),
        QStringLiteral("order the widgets were made in."),
    };
}

/// The first file of a path at or above the working directory or the executable's folder.
[[nodiscard]] QString repositoryFile(const QString& path)
{
    for (const QString& seed : {QDir::currentPath(), QCoreApplication::applicationDirPath()})
    {
        QDir folder(seed);
        do
        {
            const QString candidate = folder.filePath(path);
            if (QFileInfo(candidate).isFile())
            {
                return candidate;
            }
        } while (folder.cdUp());
    }
    return {};
}

[[nodiscard]] QString number(const double value)
{
    return QString::number(value, 'g', 12);
}

[[nodiscard]] QString sizeText(const QSize& size)
{
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

[[nodiscard]] QString policyText(QSizePolicy::Policy policy)
{
    switch (policy)
    {
    case QSizePolicy::Fixed:
        return QStringLiteral("Fixed");
    case QSizePolicy::Minimum:
        return QStringLiteral("Minimum");
    case QSizePolicy::Maximum:
        return QStringLiteral("Maximum");
    case QSizePolicy::Preferred:
        return QStringLiteral("Preferred");
    case QSizePolicy::MinimumExpanding:
        return QStringLiteral("MinimumExpanding");
    case QSizePolicy::Expanding:
        return QStringLiteral("Expanding");
    case QSizePolicy::Ignored:
        return QStringLiteral("Ignored");
    }
    return QString::number(static_cast<int>(policy));
}

[[nodiscard]] QString marksOf(const QWidget& widget)
{
    QStringList marks;
    QList<QByteArray> names = widget.dynamicPropertyNames();
    std::sort(names.begin(), names.end());
    for (const QByteArray& name : names)
    {
        if (!name.startsWith("an"))
        {
            continue;
        }
        const QVariant value = widget.property(name.constData());
        marks.append(QString::fromLatin1(name) + QLatin1Char('=') +
                     (value.typeId() == QMetaType::Bool
                          ? (value.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                          : value.toString()));
    }
    return marks.join(QStringLiteral(", "));
}

[[nodiscard]] QString stateOf(const QWidget& widget)
{
    QStringList state;
    if (widget.isHidden())
    {
        state.append(QStringLiteral("hidden"));
    }
    if (!widget.isEnabled())
    {
        state.append(QStringLiteral("disabled"));
    }
    return state.join(QStringLiteral(", "));
}

[[nodiscard]] QString contentOf(const QWidget& widget)
{
    QStringList content;
    if (const auto* banner = dynamic_cast<const an::ui::AnInfoBanner*>(&widget))
    {
        content.append(QStringLiteral("message=") + banner->message());
        content.append(QStringLiteral("tone=") + QString::number(static_cast<int>(banner->tone())));
    }
    if (const auto* card = dynamic_cast<const an::ui::AnCardWidget*>(&widget))
    {
        content.append(QStringLiteral("title=") + card->title());
    }
    if (const auto* section = dynamic_cast<const an::ui::AnCollapsibleSection*>(&widget))
    {
        content.append(QStringLiteral("title=") + section->title());
        content.append(section->isExpanded() ? QStringLiteral("expanded") : QStringLiteral("collapsed"));
    }
    if (const auto* label = qobject_cast<const QLabel*>(&widget))
    {
        content.append(QStringLiteral("text=") + label->text());
        content.append(QStringLiteral("wrap=") + (label->wordWrap() ? QStringLiteral("yes") : QStringLiteral("no")));
        content.append(QStringLiteral("align=") + QString::number(static_cast<int>(label->alignment())));
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(&widget))
    {
        content.append(QStringLiteral("text=") + button->text());
        if (button->isCheckable())
        {
            content.append(button->isChecked() ? QStringLiteral("checked") : QStringLiteral("unchecked"));
        }
    }
    if (const auto* edit = qobject_cast<const QLineEdit*>(&widget))
    {
        content.append(QStringLiteral("text=") + edit->text());
        content.append(QStringLiteral("placeholder=") + edit->placeholderText());
        content.append(QStringLiteral("echo=") + QString::number(static_cast<int>(edit->echoMode())));
        if (edit->isReadOnly())
        {
            content.append(QStringLiteral("read only"));
        }
    }
    if (const auto* spin = qobject_cast<const QDoubleSpinBox*>(&widget))
    {
        content.append(QStringLiteral("range=") + number(spin->minimum()) + QStringLiteral("..") +
                       number(spin->maximum()));
        content.append(QStringLiteral("step=") + number(spin->singleStep()));
        content.append(QStringLiteral("decimals=") + QString::number(spin->decimals()));
        content.append(QStringLiteral("value=") + number(spin->value()));
    }
    if (const auto* spin = qobject_cast<const QSpinBox*>(&widget))
    {
        content.append(QStringLiteral("range=") + QString::number(spin->minimum()) + QStringLiteral("..") +
                       QString::number(spin->maximum()));
        content.append(QStringLiteral("step=") + QString::number(spin->singleStep()));
        content.append(QStringLiteral("value=") + QString::number(spin->value()));
    }
    if (const auto* slider = qobject_cast<const QAbstractSlider*>(&widget))
    {
        content.append(QStringLiteral("range=") + QString::number(slider->minimum()) + QStringLiteral("..") +
                       QString::number(slider->maximum()));
        content.append(QStringLiteral("steps=") + QString::number(slider->singleStep()) + QLatin1Char('/') +
                       QString::number(slider->pageStep()));
        content.append(QStringLiteral("value=") + QString::number(slider->value()));
        content.append(QStringLiteral("tracking=") +
                       (slider->hasTracking() ? QStringLiteral("yes") : QStringLiteral("no")));
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(&widget))
    {
        QStringList entries;
        const auto* model = qobject_cast<const QStandardItemModel*>(combo->model());
        for (int index = 0; index < combo->count(); ++index)
        {
            QString entry = combo->itemText(index) + QLatin1Char('=') + combo->itemData(index).toString();
            const QString tip = combo->itemData(index, Qt::ToolTipRole).toString();
            if (!tip.isEmpty())
            {
                entry += QStringLiteral(" (") + tip + QLatin1Char(')');
            }
            if (model && model->item(index) && !model->item(index)->isEnabled())
            {
                entry += QStringLiteral(" off");
            }
            entries.append(entry);
        }
        content.append(QStringLiteral("entries=") + entries.join(QStringLiteral("; ")));
        content.append(QStringLiteral("current=") + QString::number(combo->currentIndex()));
    }
    if (const auto* tabs = qobject_cast<const QTabWidget*>(&widget))
    {
        QStringList titles;
        for (int index = 0; index < tabs->count(); ++index)
        {
            titles.append(tabs->tabText(index));
        }
        content.append(QStringLiteral("tabs=") + titles.join(QStringLiteral("; ")));
        content.append(QStringLiteral("current=") + QString::number(tabs->currentIndex()));
    }
    if (const auto* text = qobject_cast<const QPlainTextEdit*>(&widget))
    {
        content.append(QStringLiteral("text=") + text->toPlainText());
        content.append(QStringLiteral("placeholder=") + text->placeholderText());
    }
    if (const auto* scroll = qobject_cast<const QScrollArea*>(&widget))
    {
        content.append(QStringLiteral("resizable=") +
                       (scroll->widgetResizable() ? QStringLiteral("yes") : QStringLiteral("no")));
    }
    else if (const auto* frame = qobject_cast<const QFrame*>(&widget);
             frame && qobject_cast<const QLabel*>(&widget) == nullptr &&
             qobject_cast<const QAbstractScrollArea*>(&widget) == nullptr)
    {
        content.append(QStringLiteral("frame=") + QString::number(static_cast<int>(frame->frameShape())) +
                       QLatin1Char('/') + QString::number(static_cast<int>(frame->frameShadow())));
    }
    return content.join(QStringLiteral(", "));
}

/// A collapsible section sizes its content to the content's height, which the host's fonts decide.
constexpr const char* kSizedToItsFonts = "anCollapsibleSectionContent";

[[nodiscard]] QString sizesOf(const QWidget& widget)
{
    QStringList sizes;
    if (widget.objectName() == QLatin1String(kSizedToItsFonts))
    {
        sizes.append(QStringLiteral("sized to its content"));
    }
    else if (widget.minimumSize() != QSize(0, 0))
    {
        sizes.append(QStringLiteral("min=") + sizeText(widget.minimumSize()));
    }
    if (widget.objectName() != QLatin1String(kSizedToItsFonts) &&
        widget.maximumSize() != QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX))
    {
        sizes.append(QStringLiteral("max=") + sizeText(widget.maximumSize()));
    }
    const QSizePolicy policy = widget.sizePolicy();
    sizes.append(QStringLiteral("policy=") + policyText(policy.horizontalPolicy()) + QLatin1Char('/') +
                 policyText(policy.verticalPolicy()));
    return sizes.join(QStringLiteral(", "));
}

[[nodiscard]] QString layoutText(const QLayout& layout)
{
    const QMargins margins = layout.contentsMargins();
    QString text = QString::fromLatin1(layout.metaObject()->className()) + QStringLiteral(" margins=%1,%2,%3,%4")
                                                                               .arg(margins.left())
                                                                               .arg(margins.top())
                                                                               .arg(margins.right())
                                                                               .arg(margins.bottom());
    if (const auto* grid = qobject_cast<const QGridLayout*>(&layout))
    {
        text += QStringLiteral(" spacing=%1/%2").arg(grid->horizontalSpacing()).arg(grid->verticalSpacing());
        QStringList columns;
        for (int column = 0; column < grid->columnCount(); ++column)
        {
            columns.append(
                QStringLiteral("%1:%2").arg(grid->columnMinimumWidth(column)).arg(grid->columnStretch(column)));
        }
        text += QStringLiteral(" columns=") + columns.join(QLatin1Char(' '));
    }
    else
    {
        text += QStringLiteral(" spacing=%1").arg(layout.spacing());
    }
    return text;
}

[[nodiscard]] QString ownLayoutOf(const QWidget& widget)
{
    return widget.layout() ? layoutText(*widget.layout()) : QString();
}

/// Where a layout puts a widget: the path of item indexes through nested layouts, with a grid's cell and a box's
/// stretch, and the alignment.
[[nodiscard]] bool placeIn(const QLayout& layout, const QWidget& widget, const QString& prefix, QString* place)
{
    for (int index = 0; index < layout.count(); ++index)
    {
        QLayoutItem* item = layout.itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        QString here = prefix.isEmpty() ? QString::number(index) : prefix + QLatin1Char('/') + QString::number(index);
        if (item->widget() == &widget)
        {
            if (const auto* grid = qobject_cast<const QGridLayout*>(&layout))
            {
                int row = 0;
                int column = 0;
                int rowSpan = 0;
                int columnSpan = 0;
                grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
                here += QStringLiteral(" cell=%1,%2").arg(row).arg(column);
            }
            else if (const auto* box = qobject_cast<const QBoxLayout*>(&layout))
            {
                here += QStringLiteral(" stretch=%1").arg(box->stretch(index));
            }
            here += QStringLiteral(" align=%1").arg(static_cast<int>(item->alignment()));
            *place = here;
            return true;
        }
        if (QLayout* nested = item->layout())
        {
            if (placeIn(*nested, widget, here + QStringLiteral(" (") + layoutText(*nested) + QLatin1Char(')'), place))
            {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] QString placeOf(const QWidget& widget)
{
    const QWidget* parent = widget.parentWidget();
    QString place;
    if (parent && parent->layout() && placeIn(*parent->layout(), widget, QString(), &place))
    {
        return place;
    }
    return QStringLiteral("outside the layout");
}

void inventoryOf(const QWidget& holder, int depth, QStringList* entries)
{
    for (QObject* child : holder.children())
    {
        auto* widget = qobject_cast<QWidget*>(child);
        if (widget == nullptr || widget->isWindow())
        {
            continue;
        }
        // Of what Qt makes inside its own widgets, only the holders of the window's widgets are walked through: a
        // scroll area's viewport and a tab widget's pages. Its scroll bars and a tab bar's buttons follow the geometry.
        const bool internal = widget->objectName().startsWith(QStringLiteral("qt_"));
        if (internal && widget->objectName() != QLatin1String("qt_scrollarea_viewport") &&
            widget->objectName() != QLatin1String("qt_tabwidget_stackedwidget"))
        {
            continue;
        }
        if (!internal)
        {
            entries->append(baselineEntry({QString::fromLatin1(widget->metaObject()->className()), widget->objectName(),
                                           marksOf(*widget), stateOf(*widget), widget->toolTip(), contentOf(*widget),
                                           sizesOf(*widget), ownLayoutOf(*widget), placeOf(*widget)},
                                          depth));
        }
        inventoryOf(*widget, internal ? depth : depth + 1, entries);
    }
}

/// The widgets of a window's parameter panel: what its scroll area holds, the holder itself left out.
[[nodiscard]] QStringList panelInventory(an::ui::extensions::ExtensionNativeProcessWindow& window)
{
    QStringList entries;
    auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    if (scroll == nullptr || scroll->widget() == nullptr)
    {
        entries.append(QStringLiteral("no parameter panel"));
        return entries;
    }
    entries.append(baselineEntry({QStringLiteral("panel"), stateOf(*scroll), ownLayoutOf(*scroll->widget())}));
    inventoryOf(*scroll->widget(), 1, &entries);
    return entries;
}

/// The widgets of a window's parameter panel the Tab key reaches, in its order.
[[nodiscard]] QStringList focusOrder(an::ui::extensions::ExtensionNativeProcessWindow& window)
{
    QStringList entries;
    auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("anNativeProcessParamScrollArea"));
    QWidget* panel = scroll ? scroll->widget() : nullptr;
    QWidget* widget = &window;
    for (int step = 0; panel != nullptr && step < 4096; ++step)
    {
        widget = widget->nextInFocusChain();
        if (widget == &window)
        {
            break;
        }
        if (!panel->isAncestorOf(widget) || widget->focusPolicy() == Qt::NoFocus)
        {
            continue;
        }
        QString id = widget->property("anParamId").toString() + widget->property("anParamActionId").toString();
        if (id.isEmpty() && widget->parentWidget())
        {
            id = widget->parentWidget()->property("anParamId").toString();
        }
        entries.append(
            baselineEntry({QString::fromLatin1(widget->metaObject()->className()), widget->objectName(), id}));
    }
    if (entries.isEmpty())
    {
        entries.append(QStringLiteral("no control takes the focus"));
    }
    return entries;
}

[[nodiscard]] QJsonObject readSchemas()
{
    QFile file(repositoryFile(QString::fromLatin1(kSchemasPath)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

TEST(NativeWindow, ControlInventoryIsUnchanged)
{
    const QJsonObject schemas = readSchemas();
    ASSERT_FALSE(schemas.isEmpty()) << "there is no " << kSchemasPath << " above the working directory";

    BaselineRecord record(QString::fromLatin1(kInventoryPath), inventoryPreamble());
    for (auto it = schemas.begin(); it != schemas.end(); ++it)
    {
        an::ui::extensions::ExtensionNativeProcessWindow window(it.key().toStdString(), "session-inventory", nullptr);
        window.setParamDefs(it.value().toArray());
        EXPECT_TRUE(record.matches(it.key(), panelInventory(window))) << it.key().toStdString();
        EXPECT_TRUE(record.matches(it.key() + QStringLiteral(" focus"), focusOrder(window))) << it.key().toStdString();
    }
}

} // namespace
