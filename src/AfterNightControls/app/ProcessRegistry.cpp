// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProcessRegistry.h"

#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <algorithm>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace
{

std::string processTitle(const an::ui::process_registry::ProcessDescriptor& descriptor)
{
    if (!descriptor.process_name.empty())
    {
        return descriptor.process_name;
    }

    if (!descriptor.display_name.empty())
    {
        return descriptor.display_name;
    }

    return "Process";
}

bool supportsActiveImageChannels(const an::ui::process_registry::ProcessDescriptor& descriptor, const int channels)
{
    if (descriptor.source_channel_filter.empty())
    {
        return true;
    }

    return std::find(descriptor.source_channel_filter.begin(), descriptor.source_channel_filter.end(), channels) !=
           descriptor.source_channel_filter.end();
}

std::string unsupportedChannelsMessage(const an::ui::process_registry::ProcessDescriptor& descriptor)
{
    if (descriptor.source_channel_filter.size() == 1)
    {
        if (descriptor.source_channel_filter.front() == 3)
        {
            return "This process requires an RGB image.";
        }
        if (descriptor.source_channel_filter.front() == 1)
        {
            return "This process requires a mono image.";
        }
    }

    return "This process does not support the selected image channel count.";
}

} // anonymous namespace

namespace an::ui::process_registry
{

LaunchGuardResult evaluateProcessAvailability(const ProcessDescriptor& descriptor)
{
    if (descriptor.is_available())
    {
        return LaunchGuardResult{};
    }

    std::string message = descriptor.availability_reason;
    if (message.empty())
    {
        switch (descriptor.availability)
        {
        case ProcessAvailability::Bootstrapping:
            message = "This extension is still bootstrapping and cannot be launched yet.";
            break;
        case ProcessAvailability::InstallNeeded:
            message = "This extension must be installed before it can be launched.";
            break;
        case ProcessAvailability::RepairNeeded:
            message = "This extension environment needs repair before it can be launched.";
            break;
        case ProcessAvailability::ValidationFailed:
            message = "This extension failed validation and cannot be launched.";
            break;
        case ProcessAvailability::Incompatible:
            message = "This extension is incompatible with the current build.";
            break;
        case ProcessAvailability::Unavailable:
        default:
            message = "This process is currently unavailable.";
            break;
        }
    }

    return LaunchGuardResult{LaunchGuardFailureReason::DescriptorUnavailable, processTitle(descriptor),
                             std::move(message)};
}

bool preparesBeforeLaunch(const ProcessDescriptor& descriptor)
{
    if (!descriptor.is_extension_process || descriptor.extension_id.empty())
    {
        return false;
    }

    switch (descriptor.availability)
    {
    case ProcessAvailability::InstallNeeded:
    case ProcessAvailability::RepairNeeded:
    case ProcessAvailability::Bootstrapping:
        return true;
    case ProcessAvailability::Available:
    case ProcessAvailability::Unavailable:
    case ProcessAvailability::ValidationFailed:
    case ProcessAvailability::Incompatible:
        break;
    }
    return false;
}

LaunchGuardResult evaluateSingleImageLaunchGuards(const ProcessDescriptor& descriptor, const LaunchGuardInput& input)
{
    const std::string title = processTitle(descriptor);

    if (!input.has_active_image)
    {
        return LaunchGuardResult{LaunchGuardFailureReason::MissingActiveImage, title, "No selected image to process"};
    }

    if (!supportsActiveImageChannels(descriptor, input.active_image_channels))
    {
        return LaunchGuardResult{LaunchGuardFailureReason::UnsupportedActiveImageChannels, title,
                                 unsupportedChannelsMessage(descriptor)};
    }

    if (descriptor.requires_still_image && input.active_image_is_sequence)
    {
        return LaunchGuardResult{LaunchGuardFailureReason::UnsupportedSequence, title,
                                 "This process supports still images only, not sequences."};
    }

    if (input.has_same_process_attached)
    {
        return LaunchGuardResult{LaunchGuardFailureReason::DuplicateProcessAttached, title,
                                 "An existing process of this type is already running for the target image"};
    }

    if (input.has_process_with_exclusive_access_attached)
    {
        return LaunchGuardResult{LaunchGuardFailureReason::ExclusiveProcessAttached, title,
                                 "An existing process running for the target image requires exclusive access, please "
                                 "close that process before executing other processes."};
    }

    if (descriptor.requires_exclusive_access && input.attached_process_count != 0)
    {
        return LaunchGuardResult{LaunchGuardFailureReason::RequiresExclusiveAccessConflict, title,
                                 "This process requires exclusive access to the target image, please close all the "
                                 "currently open processes using this image before executing the process."};
    }

    return LaunchGuardResult{};
}

LaunchGuardInput launchGuardInputFor(const ProcessDescriptor& descriptor, const ImageViewControl* view)
{
    LaunchGuardInput input;
    const std::optional<an::ImageDescriptor> image =
        view != nullptr ? view->imageDescriptor() : std::optional<an::ImageDescriptor>{};
    input.has_active_image = image.has_value();
    if (!input.has_active_image)
    {
        return input;
    }

    input.active_image_channels = static_cast<int>(image->extent().channelCount());
    input.active_image_is_sequence = view->isSequence();

    const std::string& process_name =
        !descriptor.process_name.empty() ? descriptor.process_name : descriptor.display_name;
    input.has_same_process_attached = !process_name.empty() && view->hasProcessAttached(process_name);
    input.has_process_with_exclusive_access_attached = view->hasProcessWithExclusiveAccessAttached();
    input.attached_process_count = static_cast<int>(view->numOfAttachedProcesses());
    return input;
}

ProcessRegistry& ProcessRegistry::instance()
{
    static ProcessRegistry registry;
    return registry;
}

bool ProcessRegistry::registerProcess(ProcessDescriptor descriptor)
{
    if (descriptor.id.empty())
    {
        return false;
    }

    if (index_by_id_.contains(descriptor.id))
    {
        return false;
    }

    const std::size_t index = descriptors_.size();
    descriptors_.push_back(std::move(descriptor));
    index_by_id_.emplace(descriptors_.back().id, index);
    return true;
}

std::size_t ProcessRegistry::removeExtensionProcesses()
{
    std::vector<ProcessDescriptor> retained_descriptors;
    retained_descriptors.reserve(descriptors_.size());

    std::size_t removed_count = 0;
    for (auto& descriptor : descriptors_)
    {
        if (descriptor.is_extension_process)
        {
            ++removed_count;
            continue;
        }

        retained_descriptors.push_back(std::move(descriptor));
    }

    descriptors_ = std::move(retained_descriptors);
    index_by_id_.clear();
    for (std::size_t index = 0; index < descriptors_.size(); ++index)
    {
        index_by_id_.emplace(descriptors_[index].id, index);
    }

    return removed_count;
}

void ProcessRegistry::clear()
{
    descriptors_.clear();
    index_by_id_.clear();
}

bool ProcessRegistry::empty() const noexcept
{
    return descriptors_.empty();
}

const ProcessDescriptor* ProcessRegistry::find(std::string_view id) const
{
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end())
    {
        return nullptr;
    }

    const std::size_t index = it->second;
    if (index >= descriptors_.size())
    {
        return nullptr;
    }

    return &descriptors_[index];
}

ProcessDescriptor* ProcessRegistry::findMutable(std::string_view id)
{
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end())
    {
        return nullptr;
    }

    const std::size_t index = it->second;
    if (index >= descriptors_.size())
    {
        return nullptr;
    }

    return &descriptors_[index];
}

const std::vector<ProcessDescriptor>& ProcessRegistry::all() const noexcept
{
    return descriptors_;
}

std::vector<const ProcessDescriptor*> ProcessRegistry::descriptorsByCategory(std::string_view category) const
{
    std::vector<const ProcessDescriptor*> descriptors;
    for (const auto& descriptor : descriptors_)
    {
        if (descriptor.category == category)
        {
            descriptors.push_back(&descriptor);
        }
    }

    std::sort(descriptors.begin(), descriptors.end(), [](const ProcessDescriptor* lhs, const ProcessDescriptor* rhs) {
        return std::tie(lhs->sort_order, lhs->display_name, lhs->id) <
               std::tie(rhs->sort_order, rhs->display_name, rhs->id);
    });

    return descriptors;
}

bool ProcessRegistry::validateUniqueDescriptorIds() const noexcept
{
    return descriptors_.size() == index_by_id_.size();
}

bool ProcessRegistry::validateCategoryIntegrity(std::string_view category, std::string* errorMessage) const
{
    const auto descriptors = descriptorsByCategory(category);
    if (descriptors.empty())
    {
        if (errorMessage)
        {
            *errorMessage = "No descriptors registered for category";
        }
        return false;
    }

    std::set<std::string> action_names;

    for (const ProcessDescriptor* descriptor : descriptors)
    {
        if (!descriptor)
        {
            if (errorMessage)
            {
                *errorMessage = "Null descriptor encountered";
            }
            return false;
        }

        if (descriptor->id.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Descriptor id is empty";
            }
            return false;
        }

        if (descriptor->display_name.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Descriptor display_name is empty for id=" + descriptor->id;
            }
            return false;
        }

        if (descriptor->action_object_name.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Descriptor action_object_name is empty for id=" + descriptor->id;
            }
            return false;
        }

        if (!action_names.insert(descriptor->action_object_name).second)
        {
            if (errorMessage)
            {
                *errorMessage = "Duplicate action_object_name in category: " + descriptor->action_object_name;
            }
            return false;
        }
    }

    return true;
}

} // namespace an::ui::process_registry
