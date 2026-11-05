// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <type_traits>

#include "app/AfterNightAppListener.h"
#include "controls/ImageProcessControl.h"
#include "controls/TopLevelProcessBaseControl.h"

TEST(ImageProcessControlParamsAndSettings, TypeHierarchyContractsHold)
{
    static_assert(std::is_base_of_v<TopLevelProcessBaseControl, ImageProcessControl>,
                  "ImageProcessControl should inherit TopLevelProcessBaseControl");
    static_assert(std::is_base_of_v<ImageViewControlListener, ImageProcessControl>,
                  "ImageProcessControl should inherit ImageViewControlListener");
    static_assert(std::is_base_of_v<AfterNightAppListener, ImageProcessControl>,
                  "ImageProcessControl should inherit AfterNightAppListener");

    SUCCEED();
}

TEST(ImageProcessControlParamsAndSettings, PublicLifecycleContractsHold)
{
    static_assert(std::is_constructible_v<ImageProcessControl, ImageViewControl*, const std::string&>,
                  "ImageProcessControl constructor contract changed");
    static_assert(std::is_destructible_v<ImageProcessControl>, "ImageProcessControl should remain destructible");

    SUCCEED();
}

TEST(ImageProcessControlParamsAndSettings, HeaderKeepsRequiredNotificationApi)
{
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyErrorMessage)>,
                  "notifyErrorMessage should remain available");
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyWarnMessage)>,
                  "notifyWarnMessage should remain available");
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyInfoMessage)>,
                  "notifyInfoMessage should remain available");

    SUCCEED();
}

TEST(ImageProcessControlParamsAndSettings, HeaderKeepsAsyncEventApi)
{
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyCustomProcessEventAsync)>,
                  "notifyCustomProcessEventAsync should remain available");
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyCustomProcessEventIDAsync)>,
                  "notifyCustomProcessEventIDAsync should remain available");
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::notifyCustomProcessCodeEventAsync)>,
                  "notifyCustomProcessCodeEventAsync should remain available");

    SUCCEED();
}

TEST(ImageProcessControlParamsAndSettings, HeaderKeepsTheResetSettingsHook)
{
    static_assert(std::is_member_function_pointer_v<decltype(&ImageProcessControl::resetSettings)>,
                  "resetSettings override hook should remain available for process implementations");

    SUCCEED();
}
