#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************01 -create_project_files****************
# A generation Premake refused leaves the projects of the last one behind, so it must not end in "Done!".
pushd ../../premake
./configure_linux.sh || { echo "Project generation failed."; exit 1; }
popd

# A target the registry no longer builds is in no generated tree, so nothing rebuilds or deletes what it built
# before; left in place it would pass for current. Its staged outputs are removed here, where generation stops
# producing it.
python3 ../tools/active_targets.py invalidate || { echo "Removing the outputs of targets the registry no longer builds failed."; exit 1; }

# === Remove old .vscode directories ===
rm -fr ../../../.vscode
rm -fr ../../../src/AfterNightEngine/.vscode
rm -fr ../../../src/AfterNightPlatform/.vscode
rm -fr ../../../src/AfterNightControls/.vscode
rm -fr ../../../src/AfterNightCLI/.vscode
rm -fr ../../../src/AfterNightApp/.vscode
rm -fr ../../../src/AfterNightExtensionProtocol/.vscode
rm -fr ../../../src/AfterNightExtensionHost/.vscode
rm -fr ../../../src/AfterNightExtensionHostLite/.vscode
rm -fr ../../../src/AfterNightExtensionRuntimeBackend/.vscode
rm -fr ../../../src/AfterNightStylePlugin/.vscode
rm -fr ../../../src/AfterNightTests/.vscode

# === Recreate directories ===
mkdir ../../../.vscode
mkdir ../../../src/AfterNightEngine/.vscode
mkdir ../../../src/AfterNightPlatform/.vscode
mkdir ../../../src/AfterNightControls/.vscode
mkdir ../../../src/AfterNightCLI/.vscode
mkdir ../../../src/AfterNightApp/.vscode
mkdir ../../../src/AfterNightExtensionProtocol/.vscode
mkdir ../../../src/AfterNightExtensionHost/.vscode
mkdir ../../../src/AfterNightExtensionHostLite/.vscode
mkdir ../../../src/AfterNightExtensionRuntimeBackend/.vscode
mkdir ../../../src/AfterNightStylePlugin/.vscode
mkdir ../../../src/AfterNightTests/.vscode

# === Copy workspace file ===
cp vscode/AfterNight.code-workspace ../../../AfterNight.code-workspace

# === Copy IntelliSense configs ===
cp vscode/AfterNightEngine_c_cpp_properties.json ../../../src/AfterNightEngine/.vscode/c_cpp_properties.json
cp vscode/AfterNightPlatform_c_cpp_properties.json ../../../src/AfterNightPlatform/.vscode/c_cpp_properties.json
cp vscode/AfterNightControls_c_cpp_properties.json ../../../src/AfterNightControls/.vscode/c_cpp_properties.json
cp vscode/AfterNightCLI_c_cpp_properties.json ../../../src/AfterNightCLI/.vscode/c_cpp_properties.json
cp vscode/AfterNightApp_c_cpp_properties.json ../../../src/AfterNightApp/.vscode/c_cpp_properties.json
cp vscode/AfterNightExtensionProtocol_c_cpp_properties.json ../../../src/AfterNightExtensionProtocol/.vscode/c_cpp_properties.json
cp vscode/AfterNightExtensionHost_c_cpp_properties.json ../../../src/AfterNightExtensionHost/.vscode/c_cpp_properties.json
cp vscode/AfterNightExtensionHostLite_c_cpp_properties.json ../../../src/AfterNightExtensionHostLite/.vscode/c_cpp_properties.json
cp vscode/AfterNightExtensionRuntimeBackend_c_cpp_properties.json ../../../src/AfterNightExtensionRuntimeBackend/.vscode/c_cpp_properties.json
cp vscode/AfterNightStylePlugin_c_cpp_properties.json ../../../src/AfterNightStylePlugin/.vscode/c_cpp_properties.json
cp vscode/AfterNightTests_c_cpp_properties.json ../../../src/AfterNightTests/.vscode/c_cpp_properties.json

cp vscode/AfterNightApp_launch.json ../../../src/AfterNightApp/.vscode/launch.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightApp/.vscode/tasks.json

cp vscode/AfterNightCLI_launch.json ../../../src/AfterNightCLI/.vscode/launch.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightCLI/.vscode/tasks.json

cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightControls/.vscode/tasks.json
cp vscode/AfterNightExtensionHost_launch.json ../../../src/AfterNightExtensionHost/.vscode/launch.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightExtensionHost/.vscode/tasks.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightExtensionProtocol/.vscode/tasks.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightExtensionHostLite/.vscode/tasks.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightExtensionRuntimeBackend/.vscode/tasks.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightStylePlugin/.vscode/tasks.json
cp vscode/AfterNightApp_tasks.json ../../../src/AfterNightPlatform/.vscode/tasks.json

echo Done!
