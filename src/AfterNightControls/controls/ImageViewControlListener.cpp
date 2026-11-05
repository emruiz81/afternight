// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/ImageViewControlListener.h"
#include "controls/ImageViewControl.h"

ImageViewControlListener::ImageViewControlListener(ImageViewControl* ctrl) : m_image_view_control(ctrl)
{
    if (m_image_view_control)
    {
        m_image_view_control->subscribeListener(this);
    }
}

void ImageViewControlListener::setViewControl(ImageViewControl* ctrl)
{
    if (m_image_view_control)
    {
        m_image_view_control->unsubscribeListener(this);
    }

    m_image_view_control = ctrl;

    if (m_image_view_control)
    {
        m_image_view_control->subscribeListener(this);
    }
}

void ImageViewControlListener::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control)
    {
        m_image_view_control = nullptr;
    }
}

ImageViewControlListener::~ImageViewControlListener()
{
    if (m_image_view_control)
    {
        m_image_view_control->unsubscribeListener(this);
    }
}
