// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewControlManager.h"
#include "controls/ImageViewControl.h"

ImageViewControlManager::ImageViewControlManager() {}

void ImageViewControlManager::registerImageViewControl(ImageViewControl* control)
{
    auto name = control->getUniqueName();
    auto ctrl = control;
    m_image_view_controls_by_addr[ctrl] = name;
    m_image_view_controls_by_name[name] = ctrl;

    m_active_view_control = control;
}

void ImageViewControlManager::renameRegisteredImageViewControl(ImageViewControl* control)
{
    auto newName = control->getUniqueName();
    std::string oldName;

    bool doRename = false;

    auto it = m_image_view_controls_by_addr.find(control);

    if (it != m_image_view_controls_by_addr.end())
    {
        oldName = it->second;
        if (oldName != newName)
        {
            doRename = true;
        }
    }

    if (doRename)
    {
        m_image_view_controls_by_addr[control] = newName;
        m_image_view_controls_by_name.erase(oldName);
        m_image_view_controls_by_name[newName] = control;
    }
}

void ImageViewControlManager::unregisterImageViewControl(ImageViewControl* control)
{
    auto name = control->getUniqueName();
    auto itctrl = m_image_view_controls_by_addr.find(control);
    if (itctrl != m_image_view_controls_by_addr.end())
    {
        m_image_view_controls_by_addr.erase(itctrl);
    }

    auto itname = m_image_view_controls_by_name.find(name);
    if (itname != m_image_view_controls_by_name.end())
    {
        m_image_view_controls_by_name.erase(itname);
    }

    if (control == m_active_view_control)
    {
        if (m_image_view_controls_by_addr.empty())
        {
            setActiveImageViewControl(nullptr);
        }
        else
        {
            setActiveImageViewControl(m_image_view_controls_by_addr.begin()->first);
        }
    }
}

ImageViewControl* ImageViewControlManager::getActiveImageViewControl()
{
    return m_active_view_control;
}

void ImageViewControlManager::setActiveImageViewControl(ImageViewControl* control)
{
    m_active_view_control = control;
}

size_t ImageViewControlManager::getNumOfImageViewControls() const
{
    return m_image_view_controls_by_addr.size();
}

ImageViewControl* ImageViewControlManager::getImageViewControlByName(const std::string& name)
{
    auto it = m_image_view_controls_by_name.find(name);
    if (it != m_image_view_controls_by_name.end())
    {
        return it->second;
    }

    return nullptr;
}

std::string ImageViewControlManager::getImageViewControlNameByAddr(ImageViewControl* control)
{
    auto it = m_image_view_controls_by_addr.find(control);
    if (it != m_image_view_controls_by_addr.end())
    {
        return it->second;
    }

    return std::string();
}

bool ImageViewControlManager::checkImageViewControlNameExists(const std::string& name)
{
    return m_image_view_controls_by_name.find(name) != m_image_view_controls_by_name.end();
}

std::string ImageViewControlManager::createImageViewControlUniqueName(const std::string& name)
{
    std::string uniqueName = name;

    size_t i = 1;
    while (m_image_view_controls_by_name.find(uniqueName) != m_image_view_controls_by_name.end())
    {
        uniqueName = fmt::format("{}_{}", name, i++);
    }

    return uniqueName;
}

std::vector<std::string> ImageViewControlManager::getListOfImageViewControlNames()
{
    std::vector<std::string> ret;
    ret.reserve(m_image_view_controls_by_name.size());
    for (auto& element : m_image_view_controls_by_name)
    {
        ret.push_back(element.first);
    }

    return ret;
}

std::vector<ImageViewControl*> ImageViewControlManager::getListOfImageViewControls()
{
    std::vector<ImageViewControl*> ret;
    ret.reserve(m_image_view_controls_by_addr.size());
    for (auto& element : m_image_view_controls_by_addr)
    {
        ret.push_back(element.first);
    }

    return ret;
}

void ImageViewControlManager::clear()
{
    m_image_view_controls_by_addr.clear();
    m_image_view_controls_by_name.clear();
    m_active_view_control = nullptr;
}

ImageViewControlManager::~ImageViewControlManager()
{
    clear();
}
