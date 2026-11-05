#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

./01-create_project_files.sh
./02-install_linux_deps.sh
./03b-build_3rdparty_libs_debug.sh
./04b-build_afternight_debug.sh
./05b-deploy_all_deps_debug.sh
