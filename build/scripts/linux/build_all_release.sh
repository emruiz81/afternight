#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

./01-create_project_files.sh
./02-install_linux_deps.sh
./03a-build_3rdparty_libs_release.sh
./04a-build_afternight_release.sh
./05a-deploy_all_deps_release.sh
