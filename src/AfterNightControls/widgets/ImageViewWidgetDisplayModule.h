// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

class QPainter;

class ImageViewWidget;

#ifndef AFTERNIGHT_IMAGE_VIEW_MODE_DEFINED
#define AFTERNIGHT_IMAGE_VIEW_MODE_DEFINED
enum class ImageViewMode : unsigned char
{
    LINEAR_MODE = 0,
    AUTOSTRETCH_MODE
};
#endif

class AFTERNIGHTCONTROLS_API ImageViewWidgetDisplayModule
{
public:
    ImageViewWidgetDisplayModule();

    ImageViewWidget* getViewWidget() const;

    virtual void onDraw(QPainter& painter) = 0;

    void enableDisplayModule(const bool enabled);

    bool isDisplayModuleEnabled() const;

    void attachToViewWidget(ImageViewWidget* widget);
    void detachFromViewWidget();

    virtual bool supportsContextMenu()
    {
        return true;
    }

    virtual void onDisplayModeChanged(ImageViewMode mode) {}
    virtual void onAutostretchContextChanged() {}

    virtual ~ImageViewWidgetDisplayModule();

protected:
    ImageViewWidget* m_ViewWidget = nullptr;
    bool m_DisplayModuleEnabled = true;
};
