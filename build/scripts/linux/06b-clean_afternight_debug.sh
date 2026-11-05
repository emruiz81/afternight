#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************06a-clean_afternight_debug****************
pushd ../../make-linux
make config=debug_x64 clean
popd

echo Done!