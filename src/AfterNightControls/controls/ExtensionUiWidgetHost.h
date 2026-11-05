// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionSdkService.h"

#include <QHash>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>
#include <string>

#include <verdigris/wobjectdefs.h>

namespace an::ui
{

class AnButtonGroup;

}

namespace an::ui::extensions
{

class AFTERNIGHTCONTROLS_API ExtensionUiWidgetHost : public QWidget
{
    W_OBJECT(ExtensionUiWidgetHost)
public:
    explicit ExtensionUiWidgetHost(QWidget* parent = nullptr);
    ~ExtensionUiWidgetHost() override = default;

    [[nodiscard]] bool applyDescription(const ExtensionUiDescription& description, std::string* error = nullptr);
    [[nodiscard]] bool applyPatchState(const ExtensionUiPatchState& patch_state, std::string* error = nullptr);
    [[nodiscard]] bool applyLocalPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                                 std::string* error = nullptr);
    void clearDescription();
    void refreshTheme();

    [[nodiscard]] QWidget* widgetById(const QString& widget_id) const;
    [[nodiscard]] QStringList widgetIds() const;
    [[nodiscard]] const std::optional<ExtensionUiDescription>& currentDescription() const noexcept;
    [[nodiscard]] const std::optional<ExtensionUiPatchState>& currentPatchState() const noexcept;
    [[nodiscard]] bool snapshotValidationParams(QCborMap* out_params, std::string* error = nullptr) const;

    void widgetEvent(const QString& widget_id, const QString& event_name, const QCborValue& value, bool coalesced)
        W_SIGNAL(widgetEvent, widget_id, event_name, value, coalesced);

private:
    [[nodiscard]] bool applyPatchOperations(const std::vector<ExtensionUiPatchOperation>& operations,
                                            std::string* error = nullptr);
    [[nodiscard]] bool applyPatchOperation(const ExtensionUiPatchOperation& operation, std::string* error = nullptr);
    void connectWidgetEventHandlers();
    void connectButtonGroupEventHandlers(const QString& widget_id, an::ui::AnButtonGroup* button_group);
    void emitWidgetEvent(const QString& widget_id, const QString& event_name, const QCborValue& value, bool coalesced);

    QVBoxLayout* root_layout_ = nullptr;
    QWidget* rendered_root_widget_ = nullptr;
    std::optional<ExtensionUiDescription> current_description_;
    std::optional<ExtensionUiPatchState> current_patch_state_;
    QHash<QString, QWidget*> widgets_by_id_;
    QHash<QString, QString> widget_types_;
    bool suppress_events_ = false;
};

} // namespace an::ui::extensions