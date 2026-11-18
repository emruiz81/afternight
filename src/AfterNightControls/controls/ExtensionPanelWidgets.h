// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The widgets of a settings tree as an extension's panel builds them (`ExtensionParamPanel`): the kinds of the
/// widgets vocabulary, each built into the widget that holds it and known by its id, its changes said through the
/// panel, patched, read and themed as the settings section has always drawn them. Used by the panel alone.

#include "app/ExtensionSdkService.h"
#include "controls/ExtensionPanelDescription.h"

#include <QCborValue>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>

class QLayout;
class QWidget;

namespace an::ui::extensions::panel_widgets
{

/// What a widget's change is said through: its id, the event's name, the value, and whether it is a step of a
/// continuous change.
using Say = std::function<void(const QString& id, const QString& name, const QCborValue& value, bool coalescable)>;

/// The widgets of a tree by their ids: the widget, its kind, the order the ids were met in, and where their changes
/// are said once they are connected.
struct WidgetIndex
{
    QHash<QString, QWidget*> byId;
    QHash<QString, QString> types;
    QStringList order;
    Say say;
};

/// Builds a node of the widgets vocabulary and what it holds under a parent, its ids written into the index.
///
/// @return the widget, or null with why.
[[nodiscard]] QWidget* buildWidget(const PanelNode& node, QWidget* parent, WidgetIndex* index, std::string* error);

/// Connects every indexed widget's changes to `say`, which the index keeps for the widgets a patch makes again.
void connectWidgets(WidgetIndex& index, const Say& say);

/// Applies a change to an indexed widget: `visible`, `enabled`, `tooltip`, and each kind's own.
[[nodiscard]] bool patchWidget(const WidgetIndex& index, const ExtensionUiPatchOperation& operation,
                               std::string* error);

/// Whether a widget of a kind holds a value.
[[nodiscard]] bool holdsValue(const QString& type);

/// A widget's value.
[[nodiscard]] bool readValue(const QWidget* widget, const QString& type, QCborValue* out, std::string* error);

/// The tokens in force reach the tree's widgets, its root and the layout that holds it.
void refreshWidgets(const WidgetIndex& index, QWidget* root, QLayout* rootLayout);

} // namespace an::ui::extensions::panel_widgets
