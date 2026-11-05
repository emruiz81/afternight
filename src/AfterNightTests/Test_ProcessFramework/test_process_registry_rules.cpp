// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process registry's own rules, over descriptors a case registers in place of the ones the processes library
/// registered - which come back when the case ends: an id is registered once, a category lists its descriptors in their
/// sort order and is refused when two share an action or one has no name, each launch mode keeps its own launch, an
/// unavailable descriptor says why, and the starless generators follow whether StarNet is configured.

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/ProcessRegistry.h"
#include "processes/StarlessGeneratorRegistry.h"

#include <QString>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::shared::RegistrySnapshot;
using an::ui::process_registry::LaunchGuardFailureReason;
using an::ui::process_registry::ProcessAvailability;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;
using an::ui::process_registry::ProcessRegistry;

/// A single-image descriptor of a category, as the processes library states one.
[[nodiscard]] ProcessDescriptor singleImage(const std::string& id, const std::string& name, const std::string& category,
                                            const std::string& action, int sortOrder, bool exclusive = false)
{
    ProcessDescriptor descriptor;
    descriptor.id = id;
    descriptor.display_name = name;
    descriptor.category = category;
    descriptor.action_object_name = action;
    descriptor.process_name = name;
    descriptor.launch_mode = ProcessLaunchMode::SingleImage;
    descriptor.requires_exclusive_access = exclusive;
    descriptor.sort_order = sortOrder;
    descriptor.launch_single_image = [](ImageViewControl*) {};
    return descriptor;
}

/// Empties the registry and registers the three geometry descriptors, out of their sort order.
void registerGeometry()
{
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    EXPECT_TRUE(
        registry.registerProcess(singleImage("geometry.rotate", "Rotate", "geometry", "actionRotate", 30, true)));
    EXPECT_TRUE(registry.registerProcess(singleImage("geometry.resize", "Resize", "geometry", "actionResize", 10)));
    EXPECT_TRUE(registry.registerProcess(singleImage("geometry.crop", "Crop", "geometry", "actionCrop", 20, true)));
}

} // namespace

// Every registered descriptor is found by its id, and the registry holds each id once.
TEST(ProcessRegistryRulesTest, EveryRegisteredDescriptorIsFoundByItsId)
{
    const RegistrySnapshot snapshot;
    registerGeometry();
    ProcessRegistry& registry = ProcessRegistry::instance();

    EXPECT_EQ(registry.all().size(), 3u);
    EXPECT_TRUE(registry.validateUniqueDescriptorIds());
    for (const char* id : {"geometry.resize", "geometry.crop", "geometry.rotate"})
    {
        const ProcessDescriptor* descriptor = registry.find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_EQ(descriptor->id, id);
    }
    EXPECT_EQ(registry.find("geometry.mirror"), nullptr);
}

// A second descriptor under an id already registered is refused, and the first stays as it was.
TEST(ProcessRegistryRulesTest, AnIdIsRegisteredOnce)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ASSERT_TRUE(registry.registerProcess(singleImage("dup.id", "First", "dup_id", "actionFirst", 10)));

    ProcessDescriptor duplicate;
    duplicate.id = "dup.id";
    duplicate.display_name = "Second";
    duplicate.category = "dup_id";
    duplicate.action_object_name = "actionSecond";
    duplicate.process_name = "Second";
    duplicate.launch_mode = ProcessLaunchMode::TopLevel;
    duplicate.launch_top_level = [] {};
    EXPECT_FALSE(registry.registerProcess(std::move(duplicate)));

    EXPECT_EQ(registry.all().size(), 1u);
    ASSERT_NE(registry.find("dup.id"), nullptr);
    EXPECT_EQ(registry.find("dup.id")->display_name, "First");
}

// A category lists its descriptors in their sort order, whatever order they were registered in, and a category whose
// descriptors each have a name and an action of their own is whole.
TEST(ProcessRegistryRulesTest, ACategoryListsItsDescriptorsInTheirSortOrder)
{
    const RegistrySnapshot snapshot;
    registerGeometry();
    ProcessRegistry& registry = ProcessRegistry::instance();

    std::string integrityError;
    EXPECT_TRUE(registry.validateCategoryIntegrity("geometry", &integrityError)) << integrityError;

    const std::vector<const ProcessDescriptor*> descriptors = registry.descriptorsByCategory("geometry");
    ASSERT_EQ(descriptors.size(), 3u);
    EXPECT_EQ(descriptors[0]->action_object_name, "actionResize");
    EXPECT_EQ(descriptors[1]->action_object_name, "actionCrop");
    EXPECT_EQ(descriptors[2]->action_object_name, "actionRotate");
    std::set<std::string> ids;
    for (const ProcessDescriptor* descriptor : descriptors)
    {
        ids.insert(descriptor->id);
    }
    EXPECT_EQ(ids.size(), 3u);
    EXPECT_TRUE(registry.descriptorsByCategory("colour").empty());
}

// One category may hold single-image, top-level and non-interactive descriptors together, each listed by its sort
// order and the category still whole.
TEST(ProcessRegistryRulesTest, ACategoryMayMixLaunchModes)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();

    ASSERT_TRUE(registry.registerProcess(singleImage("mixed.single", "Single", "mixed", "actionSingle", 30)));

    ProcessDescriptor topLevel;
    topLevel.id = "mixed.top";
    topLevel.display_name = "Top";
    topLevel.category = "mixed";
    topLevel.action_object_name = "actionTop";
    topLevel.process_name = "Top";
    topLevel.launch_mode = ProcessLaunchMode::TopLevel;
    topLevel.sort_order = 10;
    topLevel.launch_top_level = [] {};
    ASSERT_TRUE(registry.registerProcess(std::move(topLevel)));

    ProcessDescriptor nonInteractive;
    nonInteractive.id = "mixed.non";
    nonInteractive.display_name = "Non";
    nonInteractive.category = "mixed";
    nonInteractive.action_object_name = "actionNon";
    nonInteractive.process_name = "Non";
    nonInteractive.launch_mode = ProcessLaunchMode::NonInteractive;
    nonInteractive.sort_order = 20;
    nonInteractive.launch_non_interactive = [] {};
    ASSERT_TRUE(registry.registerProcess(std::move(nonInteractive)));

    std::string integrityError;
    EXPECT_TRUE(registry.validateCategoryIntegrity("mixed", &integrityError)) << integrityError;
    const std::vector<const ProcessDescriptor*> descriptors = registry.descriptorsByCategory("mixed");
    ASSERT_EQ(descriptors.size(), 3u);
    EXPECT_EQ(descriptors[0]->id, "mixed.top");
    EXPECT_EQ(descriptors[1]->id, "mixed.non");
    EXPECT_EQ(descriptors[2]->id, "mixed.single");
}

// A category in which two descriptors share an action is refused, and the refusal names the duplicate action.
TEST(ProcessRegistryRulesTest, ACategoryWithTwoDescriptorsOnOneActionIsRefused)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ASSERT_TRUE(registry.registerProcess(singleImage("dup.a", "A", "dup", "actionDup", 10)));

    ProcessDescriptor second;
    second.id = "dup.b";
    second.display_name = "B";
    second.category = "dup";
    second.action_object_name = "actionDup";
    second.process_name = "B";
    second.launch_mode = ProcessLaunchMode::TopLevel;
    second.launch_top_level = [] {};
    ASSERT_TRUE(registry.registerProcess(std::move(second)));

    std::string integrityError;
    EXPECT_FALSE(registry.validateCategoryIntegrity("dup", &integrityError));
    EXPECT_NE(integrityError.find("Duplicate action_object_name"), std::string::npos) << integrityError;
}

// A category holding a descriptor with no name to show is refused, and the refusal says the name is empty.
TEST(ProcessRegistryRulesTest, ACategoryWithANamelessDescriptorIsRefused)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ProcessDescriptor nameless = singleImage("invalid.no_display", "", "invalid", "actionNoDisplay", 10);
    nameless.process_name = "NoDisplay";
    ASSERT_TRUE(registry.registerProcess(std::move(nameless)));

    std::string integrityError;
    EXPECT_FALSE(registry.validateCategoryIntegrity("invalid", &integrityError));
    EXPECT_NE(integrityError.find("display_name is empty"), std::string::npos) << integrityError;
}

// A top-level descriptor keeps the launch it was registered with and a non-interactive one its own, and running each
// launch runs what was registered.
TEST(ProcessRegistryRulesTest, EachLaunchModeKeepsItsOwnLaunch)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();

    bool topLevelLaunched = false;
    bool nonInteractiveLaunched = false;

    ProcessDescriptor topLevel;
    topLevel.id = "test.top_level";
    topLevel.display_name = "Top Level Test";
    topLevel.category = "test";
    topLevel.action_object_name = "actionTopLevelTest";
    topLevel.process_name = "Top Level Test";
    topLevel.launch_mode = ProcessLaunchMode::TopLevel;
    topLevel.sort_order = 10;
    topLevel.launch_top_level = [&topLevelLaunched]() { topLevelLaunched = true; };
    ASSERT_TRUE(registry.registerProcess(std::move(topLevel)));

    ProcessDescriptor nonInteractive;
    nonInteractive.id = "test.non_interactive";
    nonInteractive.display_name = "Non-Interactive Test";
    nonInteractive.category = "test";
    nonInteractive.action_object_name = "actionNonInteractiveTest";
    nonInteractive.process_name = "Non-Interactive Test";
    nonInteractive.launch_mode = ProcessLaunchMode::NonInteractive;
    nonInteractive.sort_order = 20;
    nonInteractive.launch_non_interactive = [&nonInteractiveLaunched]() { nonInteractiveLaunched = true; };
    ASSERT_TRUE(registry.registerProcess(std::move(nonInteractive)));

    const ProcessDescriptor* registeredTopLevel = registry.find("test.top_level");
    ASSERT_NE(registeredTopLevel, nullptr);
    ASSERT_TRUE(static_cast<bool>(registeredTopLevel->launch_top_level));
    EXPECT_FALSE(static_cast<bool>(registeredTopLevel->launch_non_interactive));
    registeredTopLevel->launch_top_level();
    EXPECT_TRUE(topLevelLaunched);
    EXPECT_FALSE(nonInteractiveLaunched);

    const ProcessDescriptor* registeredNonInteractive = registry.find("test.non_interactive");
    ASSERT_NE(registeredNonInteractive, nullptr);
    ASSERT_TRUE(static_cast<bool>(registeredNonInteractive->launch_non_interactive));
    EXPECT_FALSE(static_cast<bool>(registeredNonInteractive->launch_top_level));
    registeredNonInteractive->launch_non_interactive();
    EXPECT_TRUE(nonInteractiveLaunched);
}

// An unavailable descriptor is refused under its own name with the reason it states.
TEST(ProcessRegistryRulesTest, AnUnavailableDescriptorSaysWhyUnderItsName)
{
    ProcessDescriptor descriptor;
    descriptor.id = "extensions.example";
    descriptor.display_name = "Example Extension";
    descriptor.process_name = "Example Extension";
    descriptor.availability = ProcessAvailability::Unavailable;
    descriptor.availability_reason = "Extension environment is not ready yet.";

    const auto result = an::ui::process_registry::evaluateProcessAvailability(descriptor);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.reason, LaunchGuardFailureReason::DescriptorUnavailable);
    EXPECT_EQ(result.title, "Example Extension");
    EXPECT_EQ(result.message, "Extension environment is not ready yet.");

    descriptor.availability = ProcessAvailability::Available;
    EXPECT_TRUE(an::ui::process_registry::evaluateProcessAvailability(descriptor).ok());
}

// A descriptor that states no reason is refused with what the person has to do for its availability: install it,
// repair its environment, or use a build it is compatible with.
TEST(ProcessRegistryRulesTest, EachUnavailabilitySaysWhatToDoWhenNoReasonIsStated)
{
    ProcessDescriptor descriptor;
    descriptor.id = "extensions.install_needed";
    descriptor.display_name = "Install Needed";
    descriptor.process_name = "Install Needed";

    descriptor.availability = ProcessAvailability::InstallNeeded;
    EXPECT_EQ(an::ui::process_registry::evaluateProcessAvailability(descriptor).message,
              "This extension must be installed before it can be launched.");
    descriptor.availability = ProcessAvailability::RepairNeeded;
    EXPECT_EQ(an::ui::process_registry::evaluateProcessAvailability(descriptor).message,
              "This extension environment needs repair before it can be launched.");
    descriptor.availability = ProcessAvailability::Incompatible;
    EXPECT_EQ(an::ui::process_registry::evaluateProcessAvailability(descriptor).message,
              "This extension is incompatible with the current build.");
}

// An extension's process whose environment is still to be installed, refreshed, repaired or finished is prepared when
// it is launched rather than refused; one that failed validation, is incompatible or is otherwise unavailable is
// refused, as a process of the application itself is.
TEST(ProcessRegistryRulesTest, AnExtensionsProcessWhoseEnvironmentIsNotReadyIsPreparedBeforeItsLaunch)
{
    ProcessDescriptor descriptor;
    descriptor.id = "extensions.preparable";
    descriptor.display_name = "Preparable";
    descriptor.is_extension_process = true;
    descriptor.extension_id = "preparable";

    for (const ProcessAvailability availability :
         {ProcessAvailability::InstallNeeded, ProcessAvailability::RepairNeeded, ProcessAvailability::Bootstrapping})
    {
        descriptor.availability = availability;
        EXPECT_TRUE(an::ui::process_registry::preparesBeforeLaunch(descriptor)) << static_cast<int>(availability);
    }
    for (const ProcessAvailability availability :
         {ProcessAvailability::Available, ProcessAvailability::ValidationFailed, ProcessAvailability::Incompatible,
          ProcessAvailability::Unavailable})
    {
        descriptor.availability = availability;
        EXPECT_FALSE(an::ui::process_registry::preparesBeforeLaunch(descriptor)) << static_cast<int>(availability);
    }

    descriptor.availability = ProcessAvailability::InstallNeeded;
    descriptor.extension_id.clear();
    EXPECT_FALSE(an::ui::process_registry::preparesBeforeLaunch(descriptor)) << "no extension to prepare";
    descriptor.extension_id = "preparable";
    descriptor.is_extension_process = false;
    EXPECT_FALSE(an::ui::process_registry::preparesBeforeLaunch(descriptor)) << "a process of the application";
}

// StarNet is offered as a starless generator only once its program is configured and answers as one: with no program
// set it is listed unavailable with what to configure, and with a program that answers it is available.
TEST(ProcessRegistryRulesTest, StarnetGeneratesStarlessImagesOnlyOnceItIsConfigured)
{
    const RegistrySnapshot snapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();

    ProcessDescriptor descriptor;
    descriptor.id = std::string(an::ui::starless_generators::kStarnetProcessId);
    descriptor.display_name = "Starnet++";
    descriptor.category = "star_object";
    descriptor.action_object_name = "actionStarnet";
    descriptor.process_name = "Starnet++";
    descriptor.provides_starless_output = true;
    ASSERT_TRUE(registry.registerProcess(std::move(descriptor)));

    an::ui::starless_generators::StarlessGeneratorQuery query;
    query.registry = &registry;
    query.selectGlobalSetting = [](const QString&, const QString& defaultValue) { return defaultValue; };
    std::vector<an::ui::starless_generators::StarlessGeneratorDescriptor> generators =
        an::ui::starless_generators::listStarlessGenerators(query);
    ASSERT_EQ(generators.size(), 1u);
    EXPECT_TRUE(generators.front().is_native_starnet);
    EXPECT_FALSE(generators.front().available);
    EXPECT_NE(generators.front().unavailable_reason.find("StarNet2"), std::string::npos);

    query.selectGlobalSetting = [](const QString&, const QString&) { return QStringLiteral("C:/Tools/starnet2.exe"); };
    query.validateStarnetExecutable = [](const QString&) { return false; };
    generators = an::ui::starless_generators::listStarlessGenerators(query);
    ASSERT_EQ(generators.size(), 1u);
    EXPECT_FALSE(generators.front().available) << "a program that does not answer as StarNet is not configured";

    query.validateStarnetExecutable = [](const QString&) { return true; };
    generators = an::ui::starless_generators::listStarlessGenerators(query);
    ASSERT_EQ(generators.size(), 1u);
    EXPECT_TRUE(generators.front().available);
    EXPECT_TRUE(generators.front().unavailable_reason.empty());
}
