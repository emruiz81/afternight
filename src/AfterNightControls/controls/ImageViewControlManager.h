// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <string>
#include <unordered_map>
#include <vector>

class ImageViewControl;

class AFTERNIGHTCONTROLS_API ImageViewControlManager
{
public:
    ImageViewControlManager();

    void registerImageViewControl(ImageViewControl* control);

    void renameRegisteredImageViewControl(ImageViewControl* control);

    void unregisterImageViewControl(ImageViewControl* control);

    ImageViewControl* getActiveImageViewControl();

    void setActiveImageViewControl(ImageViewControl* control);

    size_t getNumOfImageViewControls() const;

    ImageViewControl* getImageViewControlByName(const std::string& name);

    std::string getImageViewControlNameByAddr(ImageViewControl* control);

    bool checkImageViewControlNameExists(const std::string& name);

    std::string createImageViewControlUniqueName(const std::string& name);

    // TODO: Offer ordering (Alphabetical, creation order, activation order, modification order, desc/asc)
    std::vector<std::string> getListOfImageViewControlNames();

    std::vector<ImageViewControl*> getListOfImageViewControls();

    void clear();

    virtual ~ImageViewControlManager();

protected:
private:
    std::unordered_map<std::string, ImageViewControl*> m_image_view_controls_by_name;

    std::unordered_map<ImageViewControl*, std::string> m_image_view_controls_by_addr;

    ImageViewControl* m_active_view_control = nullptr;
};
