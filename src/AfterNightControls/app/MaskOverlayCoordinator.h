// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ActionLookup.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>

#include <QString>

class QAction;
class ImageViewControl;
class ImageViewControlManager;

enum class MaskRenderingMode : unsigned char;

namespace an::ui
{

inline constexpr size_t kMaskOverlayActionCount = 8;

inline bool canApplyMaskOverlay(const bool hasActiveControl)
{
    return hasActiveControl;
}

inline bool shouldForceSelectedOverlayCheckedState(const bool checked)
{
    return !checked;
}

inline std::array<bool, kMaskOverlayActionCount> buildExclusiveMaskOverlaySelection(const size_t selectedIndex)
{
    std::array<bool, kMaskOverlayActionCount> selection{};
    selection.fill(false);
    if (selectedIndex < selection.size())
    {
        selection[selectedIndex] = true;
    }
    return selection;
}

inline size_t countSelectedMaskOverlayActions(const std::array<bool, kMaskOverlayActionCount>& selection)
{
    size_t count = 0;
    for (const bool selected : selection)
    {
        if (selected)
        {
            ++count;
        }
    }

    return count;
}

class AFTERNIGHTCONTROLS_API MaskOverlayCoordinator
{
public:
    struct Dependencies
    {
        /// The application's actions: the overlay entries are kept exclusive among them.
        ActionLookup actions;
        ImageViewControlManager* imageViewControlManager = nullptr;
        std::function<void(const QString&, const QString&)> notifyErrorMessage;
    };

    explicit MaskOverlayCoordinator(const Dependencies& dependencies);

    void onInvertMaskClicked(bool checked) const;
    void onEnableMaskClicked(bool checked) const;
    void onShowMaskClicked(bool checked) const;
    void onOverlayRedClicked(bool checked) const;
    void onOverlayGreenClicked(bool checked) const;
    void onOverlayBlueClicked(bool checked) const;
    void onOverlayYellowClicked(bool checked) const;
    void onOverlayCyanClicked(bool checked) const;
    void onOverlayMagentaClicked(bool checked) const;
    void onOverlayMultiplyClicked(bool checked) const;
    void onOverlayReplaceClicked(bool checked) const;

private:
    std::array<QAction*, kMaskOverlayActionCount> overlayActions() const;
    [[nodiscard]] QAction* action(std::string_view objectName) const;
    ImageViewControl* activeControlOrNotify(const QString& title) const;
    void applyOverlayMode(bool checked, QAction* selectedAction, MaskRenderingMode mode) const;

    Dependencies m_dependencies;
};

} // namespace an::ui
