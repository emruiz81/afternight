// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ExtensionUiWidgetHost.h"

#include "controls/ExtensionPanelDescription.h"

#include <QCborMap>
#include <QCborValue>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

namespace an::ui::extensions
{

W_OBJECT_IMPL(ExtensionUiWidgetHost)

ExtensionUiWidgetHost::ExtensionUiWidgetHost(QWidget* parent) : ExtensionParamPanel(parent)
{
    setObjectName(QStringLiteral("extensionUiWidgetHost"));

    // Whoever hears a tree hears each change as it is made: the settings section reacts to every one itself.
    setEventDelivery(EventDelivery::EachChange);
    QObject::connect(this, &ExtensionParamPanel::controlEvent, this,
                     [this](const QString& id, const QString& name, const QCborValue& value, const bool coalescable) {
                         if (current_description_.has_value())
                         {
                             widgetEvent(id, name, value, coalescable);
                         }
                     });
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

    ExtensionPanelDescription panel_description;
    if (!panelDescriptionFromWidgetTree(root_it->toMap(), &panel_description, error) ||
        !setDescription(panel_description, error))
    {
        return false;
    }

    current_description_ = description;
    current_patch_state_.reset();
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

    std::vector<ExtensionPanelPatch> patches;
    patches.reserve(operations.size());
    for (const ExtensionUiPatchOperation& operation : operations)
    {
        patches.push_back({operation.widget_id, operation.property_name, operation.value});
    }
    if (!applyPatches(patches, error))
    {
        return false;
    }

    refreshTheme();
    return true;
}

void ExtensionUiWidgetHost::clearDescription()
{
    clear();
    current_description_.reset();
    current_patch_state_.reset();
}

void ExtensionUiWidgetHost::refreshTheme()
{
    ExtensionParamPanel::refreshTheme();
}

QWidget* ExtensionUiWidgetHost::widgetById(const QString& widget_id) const
{
    return treeWidget(widget_id);
}

QStringList ExtensionUiWidgetHost::widgetIds() const
{
    QStringList ids = treeWidgetIds();
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
        if (!treeWidget(widget_id))
        {
            if (error)
            {
                *error = "Validation snapshot references a widget that is not rendered: " + widget_id.toStdString();
            }
            return false;
        }
        QCborValue value;
        bool holds = false;
        if (!treeWidgetValue(widget_id, &value, &holds, error))
        {
            return false;
        }
        if (holds)
        {
            out_params->insert(widget_id, value);
        }
    }

    return true;
}

} // namespace an::ui::extensions
