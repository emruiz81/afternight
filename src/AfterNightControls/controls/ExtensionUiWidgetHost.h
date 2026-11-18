// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A tree of widgets an extension describes - its settings, a panel sent through the protocol (`ui.describe`) - drawn
/// by the extension's panel. The host keeps the protocol's terms: the description and the patches as they came, with
/// their extension and session, a patch refused when it is not the session's, and each change said as `widgetEvent`
/// the moment it is made.

#include "AfterNightControlsExport.h"

#include "app/ExtensionSdkService.h"
#include "controls/ExtensionParamPanel.h"

#include <QCborMap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>
#include <string>
#include <vector>

#include <verdigris/wobjectdefs.h>

namespace an::ui::extensions
{

class AFTERNIGHTCONTROLS_API ExtensionUiWidgetHost : public ExtensionParamPanel
{
    W_OBJECT(ExtensionUiWidgetHost)
public:
    explicit ExtensionUiWidgetHost(QWidget* parent = nullptr);
    ~ExtensionUiWidgetHost() override = default;

    /// Reads the description's tree into the panel's description and builds it; refused, with why, when the tree is
    /// not one or the description is not valid.
    [[nodiscard]] bool applyDescription(const ExtensionUiDescription& description, std::string* error = nullptr);

    /// Applies a patch of the description's session, refused whole when it is another's or a change cannot be made.
    [[nodiscard]] bool applyPatchState(const ExtensionUiPatchState& patch_state, std::string* error = nullptr);

    /// Applies changes the application makes itself to what is built.
    [[nodiscard]] bool applyLocalPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                                 std::string* error = nullptr);
    void clearDescription();
    void refreshTheme();

    [[nodiscard]] QWidget* widgetById(const QString& widget_id) const;
    [[nodiscard]] QStringList widgetIds() const;
    [[nodiscard]] const std::optional<ExtensionUiDescription>& currentDescription() const noexcept;
    [[nodiscard]] const std::optional<ExtensionUiPatchState>& currentPatchState() const noexcept;

    /// The value of every widget of the description that holds one, by its id.
    [[nodiscard]] bool snapshotValidationParams(QCborMap* out_params, std::string* error = nullptr) const;

    void widgetEvent(const QString& widget_id, const QString& event_name, const QCborValue& value, bool coalesced)
        W_SIGNAL(widgetEvent, widget_id, event_name, value, coalesced);

private:
    [[nodiscard]] bool applyPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                            std::string* error = nullptr);

    std::optional<ExtensionUiDescription> current_description_;
    std::optional<ExtensionUiPatchState> current_patch_state_;
};

} // namespace an::ui::extensions
