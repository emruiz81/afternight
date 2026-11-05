// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;

namespace an::ui::process_registry
{

enum class ProcessLaunchMode
{
    SingleImage,
    TopLevel,
    NonInteractive
};

enum class ProcessAvailability
{
    Available,
    Unavailable,
    Bootstrapping,
    InstallNeeded,
    RepairNeeded,
    ValidationFailed,
    Incompatible
};

struct ProcessDescriptor
{
    std::string id;
    std::string display_name;
    std::string category;
    std::string action_object_name;
    std::string icon_key; // Reserved for future use; currently set via MenuActionManager::setActionIcons().
    std::string process_name;
    ProcessLaunchMode launch_mode = ProcessLaunchMode::SingleImage;
    bool requires_exclusive_access = false;
    bool requires_still_image = false;
    std::vector<int> source_channel_filter;
    int sort_order = 100;
    std::function<void(ImageViewControl*)> launch_single_image;
    std::function<bool(ImageViewControl*, std::string* error)> launch_single_image_checked;
    std::function<bool(ImageViewControl*, std::string* error)> launch_single_image_transient_checked;
    std::function<void()> launch_top_level;
    std::function<void()> launch_non_interactive;
    ProcessAvailability availability = ProcessAvailability::Available;
    std::string availability_reason;
    bool is_extension_process = false;
    std::string extension_id;
    bool provides_starless_output = false;
    bool provides_star_mask_output = false;

    [[nodiscard]] bool is_available() const noexcept
    {
        return availability == ProcessAvailability::Available;
    }
};

enum class LaunchGuardFailureReason
{
    None = 0,
    DescriptorUnavailable,
    MissingActiveImage,
    UnsupportedActiveImageChannels,
    UnsupportedSequence,
    DuplicateProcessAttached,
    ExclusiveProcessAttached,
    RequiresExclusiveAccessConflict
};

struct LaunchGuardInput
{
    bool has_active_image = false;
    bool active_image_is_sequence = false;
    int active_image_channels = 0;
    bool has_same_process_attached = false;
    bool has_process_with_exclusive_access_attached = false;
    int attached_process_count = 0;
};

struct LaunchGuardResult
{
    LaunchGuardFailureReason reason = LaunchGuardFailureReason::None;
    std::string title;
    std::string message;

    [[nodiscard]] bool ok() const noexcept
    {
        return reason == LaunchGuardFailureReason::None;
    }
};

AFTERNIGHTCONTROLS_API LaunchGuardResult evaluateSingleImageLaunchGuards(const ProcessDescriptor& descriptor,
                                                                         const LaunchGuardInput& input);
AFTERNIGHTCONTROLS_API LaunchGuardResult evaluateProcessAvailability(const ProcessDescriptor& descriptor);

/// Whether a launch of an unavailable descriptor prepares its process rather than refusing it: an extension's process
/// whose environment is still to be installed, refreshed or repaired, or is being prepared, is prepared - its
/// environment made ready - and then launched. A descriptor available, refused by validation or compatibility, or of
/// the application itself is not.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool preparesBeforeLaunch(const ProcessDescriptor& descriptor);

/// What a descriptor's guards read of the view its process would open on: whether the view shows an image, its channel
/// count, whether it is a sequence, and the processes attached to it - the one the descriptor's process name names
/// among them. The menu's action state and the launcher read a view through this one function, so a descriptor's
/// guards decide alike wherever they are evaluated. The image's description is read without a lease, so an edit
/// holding the view's writer never makes the caller wait.
///
/// @param descriptor the process's descriptor; its process name, or its display name when it states none, is the name
///        the process attaches to a view under.
/// @param view the view, or null for none.
/// @return the guards' input; a null view, or one that shows no image, has no active image.
AFTERNIGHTCONTROLS_API LaunchGuardInput launchGuardInputFor(const ProcessDescriptor& descriptor,
                                                            const ImageViewControl* view);

class AFTERNIGHTCONTROLS_API ProcessRegistry
{
public:
    static ProcessRegistry& instance();

    bool registerProcess(ProcessDescriptor descriptor);
    std::size_t removeExtensionProcesses();
    void clear();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const ProcessDescriptor* find(std::string_view id) const;
    [[nodiscard]] ProcessDescriptor* findMutable(std::string_view id);
    [[nodiscard]] const std::vector<ProcessDescriptor>& all() const noexcept;
    [[nodiscard]] std::vector<const ProcessDescriptor*> descriptorsByCategory(std::string_view category) const;

    [[nodiscard]] bool validateUniqueDescriptorIds() const noexcept;
    [[nodiscard]] bool validateCategoryIntegrity(std::string_view category, std::string* errorMessage = nullptr) const;

private:
    ProcessRegistry() = default;

    std::vector<ProcessDescriptor> descriptors_;
    std::map<std::string, std::size_t, std::less<>> index_by_id_;
};

} // namespace an::ui::process_registry
