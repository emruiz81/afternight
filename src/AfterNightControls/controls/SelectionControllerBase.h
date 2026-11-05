// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <type_traits>

/// CRTP base class for selection controllers that manage viewport selection state.
///
/// Derived classes **must** implement:
///   void onSelectionEnabledChanged(bool enabled)
///     Called whenever the selection state is toggled. The implementation should
///     update any internal UI or state that depends on the selection being active
///     (e.g. showing/hiding overlays, enabling/disabling mouse tracking).
///
/// Example:
///   class MySelectionController : public SelectionControllerBase<MySelectionController> { ... };
template <typename Derived>
class SelectionControllerBase : public ImageViewScrollAreaListener, public ImageViewWidgetDisplayModule
{
public:
    explicit SelectionControllerBase(ImageViewScrollArea* parent) : ImageViewScrollAreaListener(parent)
    {
        static_assert(
            std::is_base_of_v<SelectionControllerBase<Derived>, Derived>,
            "SelectionControllerBase<Derived>: Derived must inherit from SelectionControllerBase<Derived> (CRTP)");

        if (m_ViewControl && m_ViewControl->getViewWidget())
        {
            m_ViewControl->getViewWidget()->enableQuickZoom(false);
            attachToViewWidget(m_ViewControl->getViewWidget());
        }
    }

    void setSelectionEnabled(bool enabled)
    {
        m_DisplayModuleEnabled = enabled;
        static_cast<Derived*>(this)->onSelectionEnabledChanged(enabled);
        if (m_ViewControl && m_ViewControl->getViewWidget())
        {
            m_ViewControl->getViewWidget()->update();
        }
    }

    [[nodiscard]] bool isSelectionEnabled() const
    {
        return m_DisplayModuleEnabled;
    }

    void setListenerEnabled(const bool enabled) override
    {
        ImageViewScrollAreaListener::setListenerEnabled(enabled);
        setSelectionEnabled(enabled);
    }

    [[nodiscard]] bool supportsContextMenu() override
    {
        return false;
    }

    void destroy()
    {
        setSelectionEnabled(false);
    }

    ~SelectionControllerBase() override
    {
        detachFromViewWidget();
        if (m_ViewControl && m_ViewControl->getViewWidget())
        {
            m_ViewControl->getViewWidget()->enableQuickZoom(true);
        }
    }
};
