#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************07-create_afternight_installers****************

pushd ../../../installer
./linux_packager.sh
popd

echo Done!